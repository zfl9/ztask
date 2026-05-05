#pragma once
#include <cstdint>
#include <cassert>
#include <bit>
#include <limits>
#include <utility>
#include <algorithm>
#include "z_list.hpp"

struct z_Timer {
    using Callback = void (*)(z_Timer *timer) noexcept;
    z_Node node;
    Callback callback;
    uint64_t expire : 54;
    uint64_t level  : 2;
    uint64_t index  : 8;
};

// todo: 分离实现到.cpp文件

struct z_TimerMgr {
    using TimerList = z_List<z_Timer, &z_Timer::node>;

    TimerList level_0[256];
    TimerList level_1[256];
    TimerList level_2[64];
    TimerList level_3[64];

    uint64_t bitset_0[4]{}; // 256 bits
    uint64_t bitset_1[4]{}; // 256 bits
    uint64_t bitset_2{};    // 64 bits
    uint64_t bitset_3{};    // 64 bits

    uint64_t current{};

    explicit z_TimerMgr(uint64_t now = 0) noexcept : current{now} {}

    ~z_TimerMgr() noexcept = default;

    static void set_bit(uint64_t* bitset, uint64_t index) noexcept {
        bitset[index / 64] |= (1ULL << (index % 64));
    }
    static void clear_bit(uint64_t* bitset, uint64_t index) noexcept {
        bitset[index / 64] &= ~(1ULL << (index % 64));
    }
    static void set_bit(uint64_t& bitset, uint64_t index) noexcept {
        bitset |= (1ULL << index);
    }
    static void clear_bit(uint64_t& bitset, uint64_t index) noexcept {
        bitset &= ~(1ULL << index);
    }

    void add_timer(z_Timer* timer) noexcept {
        assert(!timer->node.linked());

        if (timer->expire <= current) {
            timer->expire = current + 1;
        }
        do_add_timer(timer);
    }

    void del_timer(z_Timer* timer) noexcept {
        if (!timer->node.linked()) {
            return;
        }

        timer->node.unlink();

        uint64_t level = timer->level;
        uint64_t index = timer->index;

        switch (level) {
            case 0:
                if (level_0[index].is_empty()) clear_bit(bitset_0, index);
                break;
            case 1:
                if (level_1[index].is_empty()) clear_bit(bitset_1, index);
                break;
            case 2:
                if (level_2[index].is_empty()) clear_bit(bitset_2, index);
                break;
            case 3:
                if (level_3[index].is_empty()) clear_bit(bitset_3, index);
                break;
            default:
                std::unreachable();
        }
    }

    // catch up (clock_monotonic in ms)
    void advance(uint64_t now) noexcept {
        while (current < now) {
            uint64_t step = advance_step();

            // avoid overflow (todo)
            if (current + step > now || current + step < current) {
                current = now;
                break;
            }
            current += step;

            int zeros = std::countr_zero(current);
            if (zeros >= 8) {
                discharge(1);
                if (zeros >= 16) {
                    discharge(2);
                    if (zeros >= 22) {
                        discharge(3);
                    }
                }
            }
            discharge(0);
        }
    }

    uint64_t advance_step() const noexcept {
        uint64_t step_0 = probe_step<0>();
        uint64_t step_cascade = 256 - (current & 0xFF);

        if (step_0 < step_cascade) {
            return step_0;
        }

        uint64_t step_1 = probe_step<1>();
        uint64_t step_2 = probe_step<2>();
        uint64_t step_3 = probe_step<3>();

        return std::min({step_0, step_1, step_2, step_3});
    }

private:
    void do_add_timer(z_Timer* timer) noexcept {
        uint64_t expire = timer->expire;
        uint64_t step = expire - current;

        if (step < 256) {
            do_add(0, expire & 0xFF, timer);
        } else if (step < 256 * 256) {
            do_add(1, (expire >> 8) & 0xFF, timer);
        } else if (step < 256 * 256 * 64) {
            do_add(2, (expire >> 16) & 0x3F, timer);
        } else {
            do_add(3, (expire >> 22) & 0x3F, timer);
        }
    }

    void do_add(int level, uint64_t index, z_Timer* timer) noexcept {
        switch (level) {
            case 0:
                level_0[index].push_tail(timer);
                set_bit(bitset_0, index);
                break;
            case 1:
                level_1[index].push_tail(timer);
                set_bit(bitset_1, index);
                break;
            case 2:
                level_2[index].push_tail(timer);
                set_bit(bitset_2, index);
                break;
            case 3:
                level_3[index].push_tail(timer);
                set_bit(bitset_3, index);
                break;
            default:
                std::unreachable();
        }

        timer->level = level;
        timer->index = index;
    }

    void discharge(int level) noexcept {
        uint64_t index = 0;
        TimerList* list = nullptr;

        switch (level) {
            case 0: index = current & 0xFF;         list = &level_0[index]; break;
            case 1: index = (current >> 8) & 0xFF;  list = &level_1[index]; break;
            case 2: index = (current >> 16) & 0x3F; list = &level_2[index]; break;
            case 3: index = (current >> 22) & 0x3F; list = &level_3[index]; break;
            default: std::unreachable();
        }

        if (list->is_empty()) return;

        TimerList tmp_list;
        tmp_list.steal_to_tail(list);

        switch (level) {
            case 0: clear_bit(bitset_0, index); break;
            case 1: clear_bit(bitset_1, index); break;
            case 2: clear_bit(bitset_2, index); break;
            case 3: clear_bit(bitset_3, index); break;
            default: std::unreachable();
        }

        if (level == 0) {
            while (z_Timer* timer = tmp_list.pop_head()) {
                timer->callback(timer);
            }
        } else {
            while (z_Timer* timer = tmp_list.pop_head()) {
                do_add_timer(timer);
            }
        }
    }

    // find_next_bit
    template <int NumWords>
    static uint64_t probe_distance(const uint64_t* words, int cur_bit) noexcept {
        // fast path
        bool empty = true;
        for (int i = 0; i < NumWords; ++i) {
            if (words[i] != 0) { empty = false; break; }
        }
        if (empty) return std::numeric_limits<uint64_t>::max();

        constexpr int total_bits = NumWords * 64;
        int start = (cur_bit + 1) % total_bits;
        int word_idx = start / 64;
        int bit_idx = start % 64;

        // find cur word
        uint64_t w = words[word_idx] & (~0ULL << bit_idx);
        if (w != 0) {
            int next_bit = word_idx * 64 + std::countr_zero(w);
            return next_bit > cur_bit ? (next_bit - cur_bit) : (total_bits - cur_bit + next_bit);
        }

        // find next word
        for (int i = 1; i < NumWords; ++i) {
            word_idx = (word_idx + 1) % NumWords;
            w = words[word_idx];
            if (w != 0) {
                int next_bit = word_idx * 64 + std::countr_zero(w);
                return next_bit > cur_bit ? (next_bit - cur_bit) : (total_bits - cur_bit + next_bit);
            }
        }

        std::unreachable();
    }

    template <int Level>
    uint64_t probe_step() const noexcept {
        if constexpr (Level == 0) {
            uint64_t cur = current & 0xFF;
            return probe_distance<4>(bitset_0, cur);
        } else if constexpr (Level == 1) {
            uint64_t cur = (current >> 8) & 0xFF;
            uint64_t step = probe_distance<4>(bitset_1, cur);
            if (step == std::numeric_limits<uint64_t>::max()) return step;
            return (step << 8) - (current & 0xFF);
        } else if constexpr (Level == 2) {
            uint64_t cur = (current >> 16) & 0x3F;
            uint64_t step = probe_distance<1>(&bitset_2, cur);
            if (step == std::numeric_limits<uint64_t>::max()) return step;
            return (step << 16) - (current & 0xFFFF);
        } else if constexpr (Level == 3) {
            uint64_t cur = (current >> 22) & 0x3F;
            uint64_t step = probe_distance<1>(&bitset_3, cur);
            if (step == std::numeric_limits<uint64_t>::max()) return step;
            return (step << 22) - (current & 0x3FFFFF);
        } else {
            std::unreachable();
        }
    }
};
