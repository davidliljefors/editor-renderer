#pragma once

#include <cstdint>

inline uint32_t count_trailing_zeros(uint32_t x) {
    if (x == 0) return 32; // No bits set, all 32 bits are zeros
    uint32_t count = 0;
    while ((x & 1) == 0) {
        x >>= 1;
        count++;
    }
    return count;
}

// Abstract SIMD types and operations
struct Simd128 {
    alignas(16) uint8_t data[16]; // 128-bit register abstraction

    static Simd128 load64(uint64_t value) {
        Simd128 reg{};
        *(uint64_t*)reg.data = value;
        return reg;
    }

    static Simd128 load128(const void* p) {
        Simd128 reg{};
        uintptr_t ptr = (uintptr_t)p;
        *(uint64_t*)reg.data = *(const uint64_t*)ptr;         // First 64 bits
        *(uint64_t*)(reg.data + 8) = *(const uint64_t*)(ptr + 8); // Second 64 bits
        return reg;
    }

    static Simd128 broadcast64(uint64_t value) {
        Simd128 reg{};
        *(uint64_t*)reg.data = value;
        *(uint64_t*)(reg.data + 8) = value;
        return reg;
    }

    static Simd128 broadcast(uint8_t value) {
        Simd128 reg{};
        for (int i = 0; i < 16; i++) reg.data[i] = value;
        return reg;
    }

    static Simd128 compare_eq(const Simd128& a, const Simd128& b) {
        Simd128 result{};
        for (int i = 0; i < 16; i++) {
            result.data[i] = (a.data[i] == b.data[i]) ? 0xFF : 0x00;
        }
        return result;
    }

    static uint32_t move_mask(const Simd128& a) {
        uint32_t mask = 0;
        for (int i = 0; i < 8; i++) {
            mask |= (a.data[i] & 0x80) ? (1 << i) : 0;
        }
        return mask;
    }
};