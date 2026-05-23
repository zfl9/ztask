#pragma once
#include <cstdint>
#include "z_list.hpp"

enum class z_Event : uint8_t {
    READY, // resource ready (fd readable/writable)
    ERROR, // resource error (fd error)
    TIMER, // timer triggered (sleep or timeout)
    CANCEL, // triggered only by task->cancel()
};

union z_Param {
    void *ptr; // a pointer to any local variable at the call site of `callback()`
    int64_t i64;
    uint64_t u64;
    int32_t i32;
    uint32_t u32;
    int err;
};

struct z_Waiter {
    using Callback = void (*)(z_Waiter *waiter, z_Event event, z_Param param) noexcept;

    z_Node node{};
    Callback callback = nullptr;

    explicit z_Waiter(Callback callback) noexcept
        : callback{callback} {}

    ~z_Waiter() noexcept = default;

    void unlink(bool reinit = true) noexcept {
        return node.unlink(reinit);
    }
};

using z_WaiterList = z_List<z_Waiter, &z_Waiter::node>;
