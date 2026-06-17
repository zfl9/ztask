#pragma once
#include <stdint.h>
#include <new>
#include <concepts>
#include <type_traits>
#include <utility>
#include "z_util.hpp"
#include "z_ref.hpp"
#include "z_waiter.hpp"
#include "z_timer.hpp"
#include "z_env.hpp"

enum class z_Event : uint8_t {
    WAITER, // trigger by waiter
    TIMER, // trigger by timer
    CANCEL, // trigger by task->cancel()
    _START, // start the task (internal)
    _NULL, // no event (sentinel)
};

struct z_EventCtx {
    union {
        z_Waiter *waiter;
        z_Timer *timer;
    } u{}; // the object that triggers the event
    void *arg = nullptr; // event argument (available only when the trigger is `u.waiter`)
};

// task interface (structured coroutine)
struct z_Task {
    z_Waiter waiter{&z_Task::waiter_cb};
    z_Timer timer{&z_Task::timer_cb, 0};
private:
    // execution flow (ref), creator (ref)
    uint32_t ref_count = 2;
    // the task has been terminated
    bool terminated = false;
    // cancellation signal received
    bool canceled = false;
    // task->resume(event, event_ctx)
    z_Event _event = z_Event::_NULL;
    z_EventCtx *_event_ctx = nullptr;

public:
    z_ref_counted(z_Task);

    void resume(z_Event event, z_EventCtx *event_ctx) noexcept {
        assert(_event == z_Event::_NULL && _event_ctx == nullptr);
        if (terminated) [[unlikely]] return;

        _event = event;
        _event_ctx = event_ctx;
        bool end = do_resume();
        _event = z_Event::_NULL;
        _event_ctx = nullptr;

        if (end) {
            terminate(); /* destroy the root-task */
            drop_ref(); /* held by execution flow, `delete this` may occur here */
        }
    }

    // called by z_spawn
    void start() noexcept {
        z_EventCtx event_ctx{};
        resume(z_Event::_START, &event_ctx);
    }

    // cancellation is RAII-safe, but may be completed asynchronously
    void cancel() noexcept {
        if (terminated || canceled) [[unlikely]] return;
        canceled = true;

        z_EventCtx event_ctx{};
        resume(z_Event::CANCEL, &event_ctx);
    }

    // the task has been terminated
    bool is_terminated() const noexcept { return terminated; }
    // cancellation signal received
    bool is_canceled() const noexcept { return canceled; }

    // accessible only when z_yield() resume
    z_Event event() const noexcept { return _event; }
    // accessible only when z_yield() resume
    z_EventCtx *event_ctx() const noexcept { return _event_ctx; }

private:
    // the `waiter` must be z_Task::waiter
    static void waiter_cb(z_Waiter *waiter, void *arg) noexcept {
        z_Task *task = z_container_of<&z_Task::waiter>(waiter);
        z_EventCtx event_ctx{
            .u = {.waiter = waiter},
            .arg = arg,
        };
        return task->resume(z_Event::WAITER, &event_ctx);
    }

    // the `timer` must be z_Task::timer
    static void timer_cb(z_Timer *timer) noexcept {
        z_Task *task = z_container_of<&z_Task::timer>(timer);
        z_EventCtx event_ctx{
            .u = {.timer = timer},
            .arg = nullptr,
        };
        return task->resume(z_Event::TIMER, &event_ctx);
    }

protected:
    z_Task() noexcept = default;
    virtual ~z_Task() noexcept = default;

    // @return true(DONE), false(YIELD)
    virtual bool do_resume() noexcept = 0;

    // dispose task, leaving only harmless zombies
    virtual void terminate() noexcept = 0;

    // return false if already terminated, true otherwise
    bool set_terminated() noexcept {
        if (terminated) [[unlikely]] return false;
        terminated = true;
        return true;
    }
};

// task fields (`z_call` is available)
// @param subtask_decls: SubTask a; SubTask b; ...
#define z_fields(subtask_decls...) \
    union z_SubTaskU; /* forward decl */ \
    void (*_z_subtask_deinit)(z_SubTaskU *u) noexcept = nullptr; \
    union z_SubTaskU { \
        subtask_decls; \
        z_SubTaskU() noexcept {} \
        ~z_SubTaskU() noexcept {} \
    } _z_subtask_u{}; \
    int32_t _z_resume_point = 0

// leaf task fields (`z_call` is not available)
#define z_leaf_fields() \
    int32_t _z_resume_point = 0

// for `struct RootTask final : z_Task { ... }`
// implement the `z_Task::do_resume` method
// implement the `z_Task::~T() + terminate()` method
#define z_root_deinit(T) \
    virtual bool do_resume() noexcept override { \
        /* call the z_function(result, z_task) */ \
        return this->operator()(z_ignore_result(), this); \
    } \
    virtual ~T() noexcept override { \
        this->terminate(); \
    } \
    virtual void terminate() noexcept override { \
        if (!this->set_terminated()) return; \
        z_subtask_deinit(this); \
        this->deinit(); \
    } \
    void deinit() noexcept

// for `struct NestedTask { ... }`
#define z_deinit(T) \
    ~T() noexcept { \
        z_subtask_deinit(this); \
        this->deinit(); \
    } \
    void deinit() noexcept

// define T::deinit() outside of the struct
#define z_deinit_def(T) \
    void T::deinit() noexcept

// internal helper method
template<typename T>
void z_subtask_deinit(T *task) noexcept {
    if constexpr (requires { task->_z_subtask_deinit; }) {
        if (task->_z_subtask_deinit) {
            task->_z_subtask_deinit(&task->_z_subtask_u);
            task->_z_subtask_deinit = nullptr;
        }
    }
}

// task's coroutine function
#define z_function(Result, param_decls...) \
    bool operator()(Result *_z_result, z_Task *_z_task, ##param_decls) noexcept

// task's coroutine function (define)
#define z_function_def(T, Result, param_decls...) \
    bool T::operator()(Result *_z_result, z_Task *_z_task, ##param_decls) noexcept

// forward to another overloaded z_function
#define z_function_call(args...) \
    this->operator()(_z_result, _z_task, ##args)

// current z_function's `result *`
#define z_result() (_z_result)

// current running `z_Task *`
#define z_current() (_z_task)

#define z_label Z_CONCAT(z_label_, __LINE__)
#define z_label_addr() ((int32_t)((intptr_t)&&z_label - (intptr_t)&&z_label_base))
#define z_resume_point() ((void *)((intptr_t)&&z_label_base + (intptr_t)this->_z_resume_point))

// place this at the beginning of the body of `z_function`
#define z_begin() \
    goto *z_resume_point(); \
    z_label_base: \

// access the waiter and timer of the current task
#define z_waiter() (&z_current()->waiter)
#define z_timer() (&z_current()->timer)

// place it on the line following `z_begin`
#define z_timer_arm(timeout) do { \
    auto __z_timer_timeout = (timeout); \
    if (__z_timer_timeout > 0) z_env::add_timer(z_timer(), __z_timer_timeout); \
} while (0)

// place it on the line above `z_return*`
#define z_timer_disarm() do { \
    z_env::del_timer(z_timer()); \
} while (0)

#define z_timer_restart(timeout) do { \
    z_timer_disarm(); \
    z_timer_arm(timeout); \
} while (0)

#define z_yield() do { \
    this->_z_resume_point = z_label_addr(); \
    return false; \
    z_label: ; \
} while (0)

// passed by `task->resume(event, event_ctx)`
#define z_event() (z_current()->event())
#define z_event_ctx() (z_current()->event_ctx())

template<typename T>
concept z_result_is_void = std::is_void_v<std::remove_pointer_t<std::remove_cvref_t<T>>>;

#define z__do_return() do { \
    this->_z_resume_point = INT32_MIN; /* fail-fast */ \
    return true; \
} while (0)

#define z_return_void() do { \
    static_assert(z_result_is_void<decltype(z_result())>); \
    (void)z_result(); \
    z__do_return(); \
} while (0)

#define z_return(value) do { \
    static_assert(!z_result_is_void<decltype(z_result())>); \
    if (z_result()) *z_result() = std::move(value); \
    z__do_return(); \
} while (0)

// z_call ignore the result
#define z_ignore_result() (nullptr)

// @param result: `Result *`, use `z_ignore_result()` to ignore
// @param args: the arguments passed to z_function, replayed when task->resume()
#define z_call(taskname, result, args...) do { \
    using z_SubTask = decltype(this->_z_subtask_u.taskname); \
    ::new (static_cast<void *>(&this->_z_subtask_u.taskname)) z_SubTask{}; \
    this->_z_subtask_deinit = [] (z_SubTaskU *u) static noexcept { u->taskname.~z_SubTask(); }; \
z_label: \
    if (!this->_z_subtask_u.taskname((result), z_current(), ##args)) { \
        this->_z_resume_point = z_label_addr(); \
        return false; \
    } \
    this->_z_subtask_u.taskname.~z_SubTask(); \
    this->_z_subtask_deinit = nullptr; \
    if (z_current()->is_canceled()) [[unlikely]] z__do_return(); \
} while (0)

// the caller owns a reference (z_Ref<z_Task>)
#define z_spawn(T, ctor_args...) ({ \
    z_Task *__z_spawn_task = new (std::nothrow) T{ctor_args}; \
    if (__z_spawn_task) [[likely]] \
        __z_spawn_task->start(); \
    z_Ref<z_Task>{__z_spawn_task}; \
})

// create and start task (fire and forget)
#define z_launch(T, ctor_args...) ((void)z_spawn(T, ctor_args))
