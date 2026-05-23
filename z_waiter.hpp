#pragma once
#include <cstdint>
#include "z_list.hpp"

// task->resume(event, param);
// accessible only when z_yield() resume.
// `switch (z_event()) {...}` in z_function
enum class z_Event : uint8_t {
    READY, // resource ready (fd readable/writable)
    ERROR, // resource error (fd error)
    TIMER, // timer triggered (sleep or timeout)
    CANCEL, // triggered only by task->cancel()
};

// task->resume(event, param);
// accessible only when z_yield() resume.
// `auto param = z_param().xxx` in z_function
union z_Param {
    void *ptr; // a pointer to any local variable at the call site of `task->resume()`
    int64_t i64;
    uint64_t u64;
    int32_t i32;
    uint32_t u32;
    int err;
};

struct z_Waiter {
    using Callback = void (*)(z_Waiter *waiter, z_Event, z_Param) noexcept;
    z_Node node{};
    Callback callback = nullptr;

    void unlink(bool reinit = true) noexcept {
        return node.unlink(reinit);
    }
};

using z_WaiterList = z_List<z_Waiter, &z_Waiter::node>;
