#include "z_timer.hpp"
#include <climits>
#include <cassert>
#include <utility>
#include <bit>

namespace {
    constexpr void set_bit(uint64_t *bitset, unsigned index) noexcept {
        bitset[index >> 6] |= (1ULL << (index & 0x3F));
    }

    constexpr void clear_bit(uint64_t *bitset, unsigned index) noexcept {
        bitset[index >> 6] &= ~(1ULL << (index & 0x3F));
    }

    constexpr bool test_bit(const uint64_t *bitset, unsigned index) noexcept {
        return bitset[index >> 6] & (1ULL << (index & 0x3F));
    }

    // pos of the next set bit (non-circular), or total_bits if not found
    template<unsigned num_words>
    constexpr unsigned next_bit(const uint64_t *words, unsigned pos) noexcept {
        constexpr auto total_bits = num_words << 6;

        unsigned start = pos + 1;
        if (start >= total_bits)
            return total_bits;

        unsigned word_idx = start >> 6;
        unsigned bit_idx = start & 0x3F;

        // find cur word
        uint64_t w = words[word_idx] & (~0ULL << bit_idx);
        if (w != 0)
            return (word_idx << 6) + std::countr_zero(w);

        // find next word
        while (++word_idx < num_words) {
            w = words[word_idx];
            if (w != 0)
                return (word_idx << 6) + std::countr_zero(w);
        }

        // not found
        return total_bits;
    }
}

void z_TimerMgr::add_timer(z_Timer *timer) noexcept {
    assert(!timer->node.linked());

    if (timer->expire <= current)
        timer->expire = current + 1;

    do_add_timer(timer);
}

void z_TimerMgr::del_timer(z_Timer *timer) noexcept {
    if (!timer->node.linked())
        return;

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
            if (level_2[index].is_empty()) clear_bit(&bitset_2, index);
            break;
        case 3:
            if (level_3[index].is_empty()) clear_bit(&bitset_3, index);
            break;
        default:
            std::unreachable();
    }
}

void z_TimerMgr::do_add_timer(z_Timer *timer) noexcept {
    uint64_t expire = timer->expire;
    uint64_t dist = expire - current;

    if (dist < 256) {
        link_timer(0, expire & 0xFF, timer);
    } else if (dist < 256 * 256) {
        link_timer(1, (expire >> 8) & 0xFF, timer);
    } else if (dist < 256 * 256 * 64) {
        link_timer(2, (expire >> 16) & 0x3F, timer);
    } else {
        link_timer(3, (expire >> 22) & 0x3F, timer);
    }
}

void z_TimerMgr::link_timer(unsigned level, unsigned index, z_Timer *timer) noexcept {
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
            set_bit(&bitset_2, index);
            break;
        case 3:
            level_3[index].push_tail(timer);
            set_bit(&bitset_3, index);
            break;
        default:
            std::unreachable();
    }

    timer->level = level;
    timer->index = index;
}

uint64_t z_TimerMgr::distance() const noexcept {
    if (bitset_0[0] | bitset_0[1] | bitset_0[2] | bitset_0[3]) {
        auto cur = current & 0xFF;
        auto dist = next_bit<4>(bitset_0, cur) - cur;
        return dist;
    }

    if (bitset_1[0] | bitset_1[1] | bitset_1[2] | bitset_1[3]) {
        auto cur = (current >> 8) & 0xFF;
        auto dist = next_bit<4>(bitset_1, cur) - cur;
        return (dist << 8) - (current & 0xFF);
    }

    if (bitset_2) {
        auto cur = (current >> 16) & 0x3F;
        auto dist = next_bit<1>(&bitset_2, cur) - cur;
        return (dist << 16) - (current & 0xFFFF);
    }

    if (bitset_3) {
        auto cur = (current >> 22) & 0x3F;
        auto dist = next_bit<1>(&bitset_3, cur) - cur;
        return (dist << 22) - (current & 0x3FFFFF);
    }

    return UINT64_MAX;
}

uint64_t z_TimerMgr::timeout() const noexcept {
    uint64_t sleep_ms = distance();

    // to prevent oversleeping
    uint64_t now = z_env::tick_time();
    if (sleep_ms != UINT64_MAX && current < now) {
        uint64_t elapsed = now - current;
        if (elapsed <= sleep_ms)
            sleep_ms -= elapsed;
        else
            sleep_ms = 0;
    }

    return sleep_ms;
}

int z_TimerMgr::epoll_timeout() const noexcept {
    uint64_t sleep_ms = timeout();

    // no timer
    if (sleep_ms == UINT64_MAX)
        return -1;

    // overflow
    if (sleep_ms > (uint64_t)(INT_MAX))
        return INT_MAX;

    return (int)sleep_ms;
}

void z_TimerMgr::update() noexcept {
    uint64_t now = z_env::tick_time();
    z_TimerList ready_list;

    while (current < now) {
        uint64_t dist = distance();
        uint64_t dist_to_now = now - current;

        if (dist > dist_to_now) {
            current = now;
            break;
        }
        current += dist;

        auto zeros = std::countr_zero(current);
        if (zeros >= 8) {
            cascade(1);
            if (zeros >= 16) {
                cascade(2);
                if (zeros >= 22) {
                    cascade(3);
                }
            }
        }

        harvest(&ready_list);
    }

    while (z_Timer *timer = ready_list.pop_head())
        timer->callback(timer);
}

void z_TimerMgr::harvest(z_TimerList *ready_list) noexcept {
    unsigned index = current & 0xFF;
    if (!test_bit(bitset_0, index)) return;
    clear_bit(bitset_0, index);
    ready_list->splice_tail(&level_0[index]);
}

void z_TimerMgr::cascade(unsigned level) noexcept {
    z_TimerList *list;

    switch (level) {
        case 1: {
            unsigned index = (current >> 8) & 0xFF;
            if (!test_bit(bitset_1, index)) return;
            clear_bit(bitset_1, index);
            list = &level_1[index];
            break;
        }
        case 2: {
            unsigned index = (current >> 16) & 0x3F;
            if (!test_bit(&bitset_2, index)) return;
            clear_bit(&bitset_2, index);
            list = &level_2[index];
            break;
        }
        case 3: {
            unsigned index = (current >> 22) & 0x3F;
            if (!test_bit(&bitset_3, index)) return;
            clear_bit(&bitset_3, index);
            list = &level_3[index];
            break;
        }
        default:
            std::unreachable();
    }

    z_TimerList tmp_list;
    tmp_list.splice_tail(list);

    while (z_Timer *timer = tmp_list.pop_head())
        do_add_timer(timer);
}
