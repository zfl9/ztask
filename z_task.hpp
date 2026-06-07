#pragma once
#include <cstdint>
#include <type_traits>
#include <utility>
#include <new>
#include "z_ref.hpp"
#include "z_util.hpp"
#include "z_waiter.hpp"
#include "z_timer.hpp"

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
    // task->resume(waker, payload)
    z_Waker _waker = z_Waker::_NULL;
    void *_payload = nullptr;

public:
    z_ref_impl(z_Task);

    void resume(z_Waker waker, void *payload) noexcept {
        assert(_waker == z_Waker::_NULL && _payload == nullptr);
        if (terminated) [[unlikely]] return;

        _waker = waker;
        _payload = payload;
        bool end = do_resume();
        _waker = z_Waker::_NULL;
        _payload = nullptr;

        if (end) {
            terminate(); /* destroy the root-task */
            drop_ref(); /* held by execution flow, `delete this` may occur here */
        }
    }

    // cancellation is RAII-safe, but may be completed asynchronously
    void cancel() noexcept {
        if (terminated || canceled) [[unlikely]] return;
        canceled = true;
        resume(z_Waker::CANCEL, nullptr);
    }

    // the task has been terminated
    bool is_terminated() const noexcept { return terminated; }
    // cancellation signal received
    bool is_canceled() const noexcept { return canceled; }

    // accessible only when z_yield() resume
    z_Waker waker() const noexcept { return _waker; }
    // accessible only when z_yield() resume
    void *payload() const noexcept { return _payload; }

    // the `waiter` must be z_Task::waiter
    static void waiter_cb(z_Waiter *w, z_Waker waker, void *payload) noexcept {
        z_Task *task = z_container_of<&z_Task::waiter>(w);
        return task->resume(waker, payload);
    }

    // the `timer` must be z_Task::timer
    static void timer_cb(z_Timer *timer) noexcept {
        z_Task *task = z_container_of<&z_Task::timer>(timer);
        return task->resume(z_Waker::TIMER, timer);
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
        /* recall z_function(result, z_task) */ \
        return this->operator()(z_no_result(), this); \
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
    bool operator()([[maybe_unused]] Result *_z_result, z_Task *_z_task, ##param_decls) noexcept

// task's coroutine function (define)
#define z_function_def(T, Result, param_decls...) \
    bool T::operator()([[maybe_unused]] Result *_z_result, z_Task *_z_task, ##param_decls) noexcept

// current z_function's `result *`
#define z_result() (_z_result)

// current running `z_Task *`
#define z_current() (_z_task)

// z_call ignore the result
#define z_no_result() (nullptr)

#define Z_LABEL Z_CONCAT(z_label_, __LINE__)
#define z_label_addr() ((int32_t)((intptr_t)&&Z_LABEL - (intptr_t)&&z_label_base))
#define z_resume_point() ((void *)((intptr_t)&&z_label_base + (intptr_t)this->_z_resume_point))

// place this at the beginning of the body of `z_function`
#define z_begin() \
    goto *z_resume_point(); \
    z_label_base: \

// access the waiter and timer of the current task
#define z_waiter() (&z_current()->waiter)
#define z_timer() (&z_current()->timer)

#define z_yield() do { \
    this->_z_resume_point = z_label_addr(); \
    return false; \
    Z_LABEL: ; \
} while (0)

// passed by `task->resume(waker, payload)`
#define z_waker() (z_current()->waker())
#define z_payload(T) (static_cast<T>(z_current()->payload()))

#define z_ret() do { \
    this->_z_resume_point = INT32_MIN; /* fail-fast */ \
    return true; \
} while (0)

#define z_return(result) do { \
    if (z_result()) *z_result() = std::move(result); \
    z_ret(); \
} while (0)

// @param result: `Result *`, use `z_no_result()` to ignore
// @param args: the arguments passed to z_function (pinned)
#define z_call(taskname, result, args...) do { \
    using z_SubTask = std::remove_reference_t<decltype(this->_z_subtask_u.taskname)>; \
    new (&this->_z_subtask_u.taskname) z_SubTask{}; \
    this->_z_subtask_deinit = [] (z_SubTaskU *u) static noexcept { u->taskname.~z_SubTask(); }; \
Z_LABEL: \
    if (!this->_z_subtask_u.taskname((result), z_current(), ##args)) { \
        this->_z_resume_point = z_label_addr(); \
        return false; \
    } \
    this->_z_subtask_u.taskname.~z_SubTask(); \
    this->_z_subtask_deinit = nullptr; \
    if (z_current()->is_canceled()) [[unlikely]] z_ret(); \
} while (0)

// the caller owns a reference (z_Ref<z_Task>)
#define z_spawn(T, ctor_args...) ({ \
    z_Task *__z_spawn_task = new (std::nothrow) T{ctor_args}; \
    if (__z_spawn_task) [[likely]] \
        __z_spawn_task->resume(z_Waker::_START, nullptr); \
    z_Ref<z_Task>{__z_spawn_task}; \
})

// create and start task (fire and forget)
#define z_launch(T, ctor_args...) ((void)z_spawn(T, ctor_args))
