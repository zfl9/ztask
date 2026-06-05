#pragma once
#include <cstdint>
#include "z_list.hpp"

enum class z_Waker : uint8_t {
    RESOURCE, // trigger by resource (ready/error)
    TIMER, // trigger by timer (sleep or timeout)
    CANCEL, // trigger by task->cancel()
    _START, // start the task (internal)
};

struct z_Waiter {
    using Callback = void (*)(z_Waiter *w, z_Waker waker, void *payload) noexcept;

    z_Node node{};
    Callback callback = nullptr;

    explicit z_Waiter(Callback callback) noexcept
        : callback{callback} {}

    ~z_Waiter() noexcept = default;

    bool linked() const noexcept {
        return node.linked();
    }

    void unlink(bool reinit = true) noexcept {
        return node.unlink(reinit);
    }
};

using z_WaiterList = z_List<z_Waiter, &z_Waiter::node>;
