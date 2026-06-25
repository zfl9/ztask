#pragma once
#include "z_list.hpp"

struct z_Waiter {
    using Callback = void (*)(z_Waiter *waiter, void *arg) noexcept;

    z_Node node{};
    Callback callback = nullptr;

    explicit z_Waiter(Callback callback) noexcept
        : callback{callback} {}

    bool linked() const noexcept {
        return node.linked();
    }

    void unlink(bool reinit = true) noexcept {
        return node.unlink(reinit);
    }
};

using z_WaiterList = z_List<z_Waiter, &z_Waiter::node>;
