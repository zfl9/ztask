#pragma once
#include <cassert>
#include <cstddef>

struct z_Node {
    z_Node *prev;
    z_Node *next;

    z_Node() noexcept : prev{this}, next{this} {}
    ~z_Node() noexcept { unlink(false); };

    z_Node(z_Node &&) = delete;
    z_Node(const z_Node &) = delete;
    z_Node &operator=(z_Node &&) = delete;
    z_Node &operator=(const z_Node &) = delete;

    void init() noexcept {
        prev = next = this;
    }

    bool linked() const noexcept {
        return prev != this;
    }

    // before <-> this <-> after
    void link(z_Node *before, z_Node *after) noexcept {
        assert(before != this);
        assert(after != this);
        before->next = this;
        prev = before;
        next = after;
        after->prev = this;
    }

    void link_before(z_Node *pos) noexcept {
        return link(pos->prev, pos);
    }

    void link_after(z_Node *pos) noexcept {
        return link(pos, pos->next);
    }

    void unlink(bool reinit = true) noexcept {
        prev->next = next;
        next->prev = prev;
        if (reinit) init();
    }

    // ================== container ==================

    bool is_empty() const noexcept {
        return !linked();
    }

    void clear() noexcept {
        while (pop_head());
    }

    void detach() noexcept {
        return unlink();
    }

    z_Node *first() const noexcept {
        return next;
    }

    z_Node *last() const noexcept {
        return prev;
    }

    void push_head(z_Node *new_node) noexcept {
        return new_node->link_after(this);
    }

    void push_tail(z_Node *new_node) noexcept {
        return new_node->link_before(this);
    }

    z_Node *pop_head() noexcept {
        if (is_empty()) return nullptr;
        z_Node *del_node = first();
        del_node->unlink();
        return del_node;
    }

    z_Node *pop_tail() noexcept {
        if (is_empty()) return nullptr;
        z_Node *del_node = last();
        del_node->unlink();
        return del_node;
    }

    void move_head(z_Node *moved_node) noexcept {
        assert(moved_node != this);
        assert(moved_node->linked());

        if (moved_node != first()) {
            moved_node->unlink(false); // reinit=false
            push_head(moved_node);
        }
    }

    void move_tail(z_Node *moved_node) noexcept {
        assert(moved_node != this);
        assert(moved_node->linked());

        if (moved_node != last()) {
            moved_node->unlink(false); // reinit=false
            push_tail(moved_node);
        }
    }

    void steal_head(z_Node *moved_root) noexcept {
        assert(moved_root != this);
        if (moved_root->is_empty()) return;

        moved_root->prev->next = this->next;
        this->next->prev = moved_root->prev;

        this->next = moved_root->next;
        moved_root->next->prev = this;

        moved_root->init();
    }

    void steal_tail(z_Node *moved_root) noexcept {
        assert(moved_root != this);
        if (moved_root->is_empty()) return;

        this->prev->next = moved_root->next;
        moved_root->next->prev = this->prev;

        moved_root->prev->next = this;
        this->prev = moved_root->prev;

        moved_root->init();
    }
};

// type-safe binding for z_Node
template<typename T, z_Node T::*node_field>
struct z_node_binding {
    z_node_binding() = delete;
    ~z_node_binding() = delete;

    static T *item_of(z_Node *node) noexcept {
        return const_cast<T *>(item_of(const_cast<const z_Node *>(node)));
    }

    static const T *item_of(const z_Node *node) noexcept {
        size_t offset = reinterpret_cast<size_t>(&(static_cast<T *>(nullptr)->*node_field));
        return reinterpret_cast<const T *>(reinterpret_cast<const char *>(node) - offset);
    }

    static z_Node *node_of(T *item) noexcept {
        return &(item->*node_field);
    }

    static const z_Node *node_of(const T *item) noexcept {
        return &(item->*node_field);
    }

    static void init(T *item) noexcept {
        return node_of(item)->init();
    }

    static bool linked(const T *item) noexcept {
        return node_of(item)->linked();
    }

    static void link_before(T *item, T *pos) noexcept {
        return node_of(item)->link_before(node_of(pos));
    }

    static void link_after(T *item, T *pos) noexcept {
        return node_of(item)->link_after(node_of(pos));
    }

    static void unlink(T *item, bool reinit = true) noexcept {
        return node_of(item)->unlink(reinit);
    }
};

template<typename T, z_Node T::*node_field>
struct z_List {
    z_Node root{};

    z_List() noexcept = default;
    ~z_List() noexcept = default;

    z_List(z_List &&) = delete;
    z_List(const z_List &) = delete;
    z_List &operator=(z_List &&) = delete;
    z_List &operator=(const z_List &) = delete;

    using binding = z_node_binding<T, node_field>;

    bool is_empty() const noexcept {
        return root.is_empty();
    }

    // pop all elements (elements become unlinked)
    void clear() noexcept {
        return root.clear();
    }

    // detach all elements (elements become a circular linked list)
    void detach() noexcept {
        return root.detach();
    }

    /// @brief get first item
    /// @return nullptr if is_empty()
    T *first() const noexcept {
        if (is_empty()) return nullptr;
        return binding::item_of(root.first());
    }

    /// @brief get last item
    /// @return nullptr if is_empty()
    T *last() const noexcept {
        if (is_empty()) return nullptr;
        return binding::item_of(root.last());
    }

    void push_head(T *new_item) noexcept {
        return root.push_head(binding::node_of(new_item));
    }

    void push_tail(T *new_item) noexcept {
        return root.push_tail(binding::node_of(new_item));
    }

    T *pop_head() noexcept {
        z_Node *del_node = root.pop_head();
        return del_node ? binding::item_of(del_node) : nullptr;
    }

    T *pop_tail() noexcept {
        z_Node *del_node = root.pop_tail();
        return del_node ? binding::item_of(del_node) : nullptr;
    }

    void move_head(T *item) noexcept {
        return root.move_head(binding::node_of(item));
    }

    void move_tail(T *item) noexcept {
        return root.move_tail(binding::node_of(item));
    }

    /// move all elements from `moved_list` to head. O(1)
    void steal_head(z_List *moved_list) noexcept {
        return root.steal_head(&moved_list->root);
    }

    /// move all elements from `moved_list` to tail. O(1)
    void steal_tail(z_List *moved_list) noexcept {
        return root.steal_tail(&moved_list->root);
    }

    struct IterEnd {};

    template<bool is_forward>
    struct Iter {
        const z_Node *const root;
        z_Node *cur;
        z_Node *next;

        Iter(const z_Node *root) noexcept : root{root} {
            if constexpr (is_forward) {
                cur = root->next;
                next = cur->next;
            } else {
                cur = root->prev;
                next = cur->prev;
            }
        }

        bool operator==(IterEnd) const noexcept {
            return cur == root;
        }

        T *operator*() const noexcept {
            return binding::item_of(cur);
        }

        T *operator->() const noexcept {
            return binding::item_of(cur);
        }

        Iter &operator++() noexcept {
            cur = next;
            if constexpr (is_forward) {
                next = cur->next;
            } else {
                next = cur->prev;
            }
            return *this;
        }
    };

    template<bool is_forward>
    struct View {
        const z_Node *const root;

        Iter<is_forward> begin() const noexcept {
            return {root};
        }

        IterEnd end() const noexcept {
            return {};
        }
    };

    View<true> items() const noexcept {
        return {&root};
    }

    View<false> rev_items() const noexcept {
        return {&root};
    }
};
