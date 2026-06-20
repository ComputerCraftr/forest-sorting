#ifndef FOREST_SORTING_DETAIL_HASH_HPP
#define FOREST_SORTING_DETAIL_HASH_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace forest_sorting::detail {

// Portable 2-limb FNV-1a 128-bit state
struct Fnv1a128State {
    uint64_t high;
    uint64_t low;
};

// FNV-1a 128-bit hash constants
inline constexpr uint64_t fnv1a128_offset_basis_high = 0x6c62272e07bb0142ULL;
inline constexpr uint64_t fnv1a128_offset_basis_low = 0x62b821756295c58dULL;
inline constexpr uint64_t fnv1a128_prime_low = 0x13bULL;

// FNV-1a 128-bit hash prime is (1 << 88) + 0x13b
// Portable 2-limb step implementation
inline void fnv1a128_step_portable(Fnv1a128State &state,
                                   uint8_t byteValue) noexcept {
    state.low ^= byteValue;

    // state *= (1 << 88) + 0x13b
    // = state * (1 << 88) + state * 0x13b
    // state * (1 << 88) only affects the high 64-bit limb of a 128-bit product
    // as state.low << 24. (state.high << 88 overflows 128 bits).

    // state * 0x13b:
    const uint64_t lo_lo = (state.low & 0xFFFFFFFFULL) * fnv1a128_prime_low;
    const uint64_t lo_hi = (state.low >> 32) * fnv1a128_prime_low;
    const uint64_t carry = (lo_hi + (lo_lo >> 32)) >> 32;

    state.high = (state.high * fnv1a128_prime_low) + (state.low << 24) + carry;
    state.low = state.low * fnv1a128_prime_low;
}

// FNV-1a 128-bit hash implementation for a byte sequence (portable path)
inline Fnv1a128State fnv1a128_hash_bytes(const uint8_t *data,
                                         std::size_t size) noexcept {
    Fnv1a128State state{fnv1a128_offset_basis_high, fnv1a128_offset_basis_low};
    for (std::size_t byteIdx = 0; byteIdx < size; ++byteIdx) {
        fnv1a128_step_portable(state, data[byteIdx]);
    }
    return state;
}

inline std::size_t fold_fnv1a128(Fnv1a128State state) noexcept {
    return static_cast<std::size_t>(state.high ^ state.low);
}

template <typename State, typename Step>
inline State fnv1a128_hash_words_msb_first(State state, uint64_t high,
                                           uint64_t low, Step step) noexcept {
    // Pre-extract bytes to allow independent shift/mask instructions before the
    // dependent hash chain.
    const std::array<uint8_t, 16> bytes = {
        static_cast<uint8_t>(high >> 56U), static_cast<uint8_t>(high >> 48U),
        static_cast<uint8_t>(high >> 40U), static_cast<uint8_t>(high >> 32U),
        static_cast<uint8_t>(high >> 24U), static_cast<uint8_t>(high >> 16U),
        static_cast<uint8_t>(high >> 8U),  static_cast<uint8_t>(high),
        static_cast<uint8_t>(low >> 56U),  static_cast<uint8_t>(low >> 48U),
        static_cast<uint8_t>(low >> 40U),  static_cast<uint8_t>(low >> 32U),
        static_cast<uint8_t>(low >> 24U),  static_cast<uint8_t>(low >> 16U),
        static_cast<uint8_t>(low >> 8U),   static_cast<uint8_t>(low)};

    for (std::size_t i = 0; i < 16; ++i) {
        step(state, bytes[i]);
    }

    return state;
}

// Portable word-based hash for generic tests.
inline std::size_t hashUint128Words(uint64_t high, uint64_t low) noexcept {
    const Fnv1a128State state = fnv1a128_hash_words_msb_first(
        Fnv1a128State{fnv1a128_offset_basis_high, fnv1a128_offset_basis_low},
        high, low, [](Fnv1a128State &hashState, uint8_t byteValue) noexcept {
            fnv1a128_step_portable(hashState, byteValue);
        });

    return fold_fnv1a128(state);
}

#ifdef __SIZEOF_INT128__

inline constexpr unsigned __int128 fnv1a128_offset_basis_native =
    (static_cast<unsigned __int128>(fnv1a128_offset_basis_high) << 64) |
    fnv1a128_offset_basis_low;
inline constexpr unsigned __int128 fnv1a128_prime_native =
    (static_cast<unsigned __int128>(0x1000000ULL) << 64) | 0x13bULL;

inline unsigned __int128 fnv1a128_step_native(unsigned __int128 hashValue,
                                              uint8_t byteValue) noexcept {
    hashValue ^= byteValue;
    hashValue *= fnv1a128_prime_native;
    return hashValue;
}

inline std::size_t fold_fnv1a128(unsigned __int128 hashValue) noexcept {
    const uint64_t high = static_cast<uint64_t>(hashValue >> 64);
    const uint64_t low = static_cast<uint64_t>(hashValue);
    return static_cast<std::size_t>(high ^ low);
}

// Native path for optional UInt128 compatibility layer.
inline std::size_t hashUint128(unsigned __int128 value) noexcept {
    const unsigned __int128 hashValue = fnv1a128_hash_words_msb_first(
        fnv1a128_offset_basis_native, static_cast<uint64_t>(value >> 64U),
        static_cast<uint64_t>(value),
        [](unsigned __int128 &hashState, uint8_t byteValue) noexcept {
            hashState = fnv1a128_step_native(hashState, byteValue);
        });

    return fold_fnv1a128(hashValue);
}

#endif // __SIZEOF_INT128__

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_HASH_HPP
