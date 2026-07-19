#ifndef FOREST_SORTING_BENCHMARK_SUPPORT_FULL_HASH_VARIANTS_HPP
#define FOREST_SORTING_BENCHMARK_SUPPORT_FULL_HASH_VARIANTS_HPP

#ifdef __SIZEOF_INT128__
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#endif

#include <array>
#include <cstddef>
#include <cstdint>

namespace forest_sorting::benchmark_support {

struct Fnv1a128State {
    uint64_t high;
    uint64_t low;
};

inline constexpr uint64_t fnv1a128_offset_basis_high = 0x6c62272e07bb0142ULL;
inline constexpr uint64_t fnv1a128_offset_basis_low = 0x62b821756295c58dULL;
inline constexpr uint64_t fnv1a128_prime_low = 0x13bULL;

inline void fnv1a128StepPortable(Fnv1a128State &state,
                                 uint8_t byteValue) noexcept {
    state.low ^= byteValue;
    const uint64_t lowLow = (state.low & 0xFFFFFFFFULL) * fnv1a128_prime_low;
    const uint64_t lowHigh = (state.low >> 32U) * fnv1a128_prime_low;
    const uint64_t carry = (lowHigh + (lowLow >> 32U)) >> 32U;
    state.high = (state.high * fnv1a128_prime_low) + (state.low << 24U) + carry;
    state.low *= fnv1a128_prime_low;
}

inline Fnv1a128State fnv1a128HashBytes(const uint8_t *data,
                                       std::size_t size) noexcept {
    Fnv1a128State state{fnv1a128_offset_basis_high, fnv1a128_offset_basis_low};
    for (std::size_t byteIndex = 0; byteIndex < size; ++byteIndex) {
        fnv1a128StepPortable(state, data[byteIndex]);
    }
    return state;
}

inline std::size_t foldFnv1a128(Fnv1a128State state) noexcept {
    return static_cast<std::size_t>(state.high ^ state.low);
}

template <typename State, typename Step>
inline State fnv1a128HashWordsMsbFirst(State state, uint64_t high, uint64_t low,
                                       Step step) noexcept {
    const std::array<uint8_t, 16> bytes = {
        static_cast<uint8_t>(high >> 56U), static_cast<uint8_t>(high >> 48U),
        static_cast<uint8_t>(high >> 40U), static_cast<uint8_t>(high >> 32U),
        static_cast<uint8_t>(high >> 24U), static_cast<uint8_t>(high >> 16U),
        static_cast<uint8_t>(high >> 8U),  static_cast<uint8_t>(high),
        static_cast<uint8_t>(low >> 56U),  static_cast<uint8_t>(low >> 48U),
        static_cast<uint8_t>(low >> 40U),  static_cast<uint8_t>(low >> 32U),
        static_cast<uint8_t>(low >> 24U),  static_cast<uint8_t>(low >> 16U),
        static_cast<uint8_t>(low >> 8U),   static_cast<uint8_t>(low)};
    for (uint8_t byteValue : bytes) {
        step(state, byteValue);
    }
    return state;
}

inline std::size_t fnvHashUInt128Words(uint64_t high, uint64_t low) noexcept {
    const Fnv1a128State state = fnv1a128HashWordsMsbFirst(
        Fnv1a128State{fnv1a128_offset_basis_high, fnv1a128_offset_basis_low},
        high, low, [](Fnv1a128State &hashState, uint8_t byteValue) noexcept {
            fnv1a128StepPortable(hashState, byteValue);
        });
    return foldFnv1a128(state);
}

#ifdef __SIZEOF_INT128__
inline constexpr UInt128 fnv1a128_offset_basis_native =
    (static_cast<UInt128>(fnv1a128_offset_basis_high) << 64U) |
    fnv1a128_offset_basis_low;
inline constexpr UInt128 fnv1a128_prime_native =
    (static_cast<UInt128>(0x1000000ULL) << 64U) | 0x13bULL;

inline UInt128 fnv1a128StepNative(UInt128 hashValue,
                                  uint8_t byteValue) noexcept {
    hashValue ^= byteValue;
    hashValue *= fnv1a128_prime_native;
    return hashValue;
}

inline std::size_t fnvHashUInt128(UInt128 value) noexcept {
    const UInt128 hashValue = fnv1a128HashWordsMsbFirst(
        fnv1a128_offset_basis_native, static_cast<uint64_t>(value >> 64U),
        static_cast<uint64_t>(value),
        [](UInt128 &state, uint8_t byteValue) noexcept {
            state = fnv1a128StepNative(state, byteValue);
        });
    return static_cast<std::size_t>(static_cast<uint64_t>(hashValue >> 64U) ^
                                    static_cast<uint64_t>(hashValue));
}
#endif

template <typename ByteRange>
inline std::size_t fnvHashBytes(const ByteRange &bytes) noexcept {
    return foldFnv1a128(fnv1a128HashBytes(bytes.data(), bytes.size()));
}

#ifdef __SIZEOF_INT128__
struct UInt128NodeHashedTraits : UInt128NodeTraits {
    static std::size_t hash(UInt128 nodeId) noexcept {
        return fnvHashUInt128(nodeId);
    }
};
#endif

} // namespace forest_sorting::benchmark_support

#endif // FOREST_SORTING_BENCHMARK_SUPPORT_FULL_HASH_VARIANTS_HPP
