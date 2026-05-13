#pragma once
#include <cstdint>
#include <type_traits>
#include <utility>
#include <new>
#include "z_list.hpp"

// task interface (stackless coroutine)
struct z_Task {
public:
    z_Node wait_node{};
private:
    // execution flow (ref), creator (ref)
    uint32_t ref_count = 2;
    // cancellation signal received
    bool canceled = false;
protected:
    // the task has been terminated
    bool terminated = false;

public:
    z_Task *ref() noexcept {
        ++ref_count;
        return this;
    }

    void unref() noexcept {
        if (--ref_count == 0)
            delete this;
    }

    // @return true(DONE), false(YIELD)
    bool resume() noexcept {
        if (terminated) [[unlikely]] return true;
        if (do_resume()) {
            terminate();
            unref();
            return true;
        }
        return false;
    }

    // cancellation is synchronous and RAII-safe
    void cancel() noexcept {
        if (terminated || canceled) [[unlikely]] return;
        canceled = true;
        resume();
    }

    bool is_canceled() const noexcept { return canceled; }

protected:
    // task must have a destructor
    virtual ~z_Task() noexcept = default;

    // @return true(DONE), false(YIELD)
    virtual bool do_resume() noexcept = 0;

    // dispose task, leaving only harmless zombies
    virtual void terminate() noexcept = 0;
};

struct z_TaskRef {
private:
    z_Task *task;

public:
    explicit z_TaskRef(z_Task *task) noexcept : task{task} {}
    ~z_TaskRef() noexcept { if (task) task->unref(); }

    z_TaskRef(z_TaskRef &&other) noexcept : task{std::exchange(other.task, nullptr)} {}
    z_TaskRef(const z_TaskRef &) = delete; // use `share()` instead

    z_TaskRef &operator=(z_TaskRef &&) = delete;
    z_TaskRef &operator=(const z_TaskRef &) = delete;

    z_Task *operator->() const noexcept { return task; }
    z_Task &operator*() const noexcept { return *task; }
    explicit operator bool() const noexcept { return task != nullptr; }

    [[nodiscard]] z_TaskRef share() const noexcept { return z_TaskRef{ task->ref() }; } // ref++
    [[nodiscard]] z_Task *raw() const noexcept { return task; } // raw pointer
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
    } _z_subtask_u; \
    int32_t _z_resume_point = 0

// leaf task fields (`z_call` is not available)
#define z_leaf_fields() \
    int32_t _z_resume_point = 0

// for `struct RootTask : z_Task { ... }`
// implement the `z_Task::do_resume` method
// implement the `z_Task::~T() + terminate()` method
#define z_impl_deinit(T) \
    virtual bool do_resume() noexcept override { \
        /* recall z_function(result, z_task) */ \
        return this->operator()(nullptr, this); \
    } \
    virtual ~T() noexcept override { \
        this->terminate(); \
    } \
    virtual void terminate() noexcept override { \
        if (this->terminated) return; \
        this->terminated = true; \
        z_subtask_deinit(this); \
        this->deinit(); \
    } \
    void deinit() noexcept

// for `struct LogicTask { ... }`
#define z_def_deinit(T) \
    ~T() noexcept { \
        z_subtask_deinit(this); \
        this->deinit(); \
    } \
    void deinit() noexcept

// internal helper method
template<typename T>
inline void z_subtask_deinit(T *task) noexcept {
    if constexpr (requires { task->_z_subtask_deinit; }) {
        if (task->_z_subtask_deinit) {
            task->_z_subtask_deinit(&task->_z_subtask_u);
            task->_z_subtask_deinit = nullptr;
        }
    }
}

// task's coroutine function
#define z_function(Result, param_decls...) bool operator()(Result *_z_result, z_Task *_z_task, ##param_decls) noexcept

// current z_function's `result *`
#define z_result() (_z_result)

// current running `z_Task *`
#define z_current() (_z_task)

#define Z_CONCAT_(a, b) a##b
#define Z_CONCAT(a, b) Z_CONCAT_(a, b)
#define Z_LABEL Z_CONCAT(z_label_, __LINE__)
#define z_label_addr ((int32_t)((intptr_t)&&Z_LABEL - (intptr_t)&&z_label_base))
#define z_resume_point ((void *)((intptr_t)&&z_label_base + (intptr_t)this->_z_resume_point))

#define z_check_cancel() do { \
    if (z_current()->is_canceled()) [[unlikely]] \
        z_ret(); \
} while (0)

// place this at the beginning of the body of `z_function`
#define z_begin() \
    goto *z_resume_point; \
    z_label_base: \
    z_check_cancel() \

#define z_yield(resume_logic...) do { \
    this->_z_resume_point = z_label_addr; \
    return false; \
    Z_LABEL: \
    resume_logic; \
    z_check_cancel(); \
} while (0)

#define z_ret(final_logic...) do { \
    this->_z_resume_point = INT32_MIN; /* fail-fast */ \
    final_logic; \
    return true; \
} while (0)

#define z_return(result, final_logic...) do { \
    if (z_result()) *z_result() = std::move(result); \
    z_ret(final_logic); \
} while (0)

#define z_no_result() nullptr

// @param result: `Result *`, use `z_no_result()` to ignore
// @param args: the arguments passed to z_function (pinned)
#define z_call(taskname, result, args...) do { \
    using z_SubTask = std::remove_reference_t<decltype(this->_z_subtask_u.taskname)>; \
    new (&this->_z_subtask_u.taskname) z_SubTask{}; \
    this->_z_subtask_deinit = [] (z_SubTaskU *u) noexcept { u->taskname.~z_SubTask(); }; \
Z_LABEL: \
    if (!this->_z_subtask_u.taskname((result), z_current(), ##args)) { \
        this->_z_resume_point = z_label_addr; \
        return false; \
    } \
    this->_z_subtask_u.taskname.~z_SubTask(); \
    this->_z_subtask_deinit = nullptr; \
    z_check_cancel(); \
} while (0)

// the caller owns a reference (z_TaskRef)
#define z_spawn(T, ctor_args...) ({ \
    z_Task *__z_spawn_task = new (std::nothrow) T{ctor_args}; \
    if (__z_spawn_task) [[likely]] \
        __z_spawn_task->resume(); \
    z_TaskRef{__z_spawn_task}; \
})

// create and start task (fire and forget)
#define z_launch(T, ctor_args...) ((void)z_spawn(T, ctor_args))
