#ifndef FOREST_SORTING_UINT128_HPP
#define FOREST_SORTING_UINT128_HPP

#ifndef __SIZEOF_INT128__
#error                                                                         \
    "forest_sorting UInt128 support requires unsigned __int128. Include forest_sorting/algorithms.hpp and use caller-owned ID types with custom traits for portable code."
#endif

#include "forest_sorting/detail/hash.hpp"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <sstream>
#include <string>

namespace forest_sorting {

using UInt128 = unsigned __int128;

inline std::string toHex(UInt128 value) {
    if (value == 0) {
        return "0x0";
    }

    std::ostringstream oss;
    oss << "0x" << std::hex;

    const auto high = static_cast<uint64_t>(value >> 64);
    const auto low = static_cast<uint64_t>(value);

    if (high != 0) {
        oss << high << std::setw(16) << std::setfill('0') << low;
    } else {
        oss << low;
    }

    return oss.str();
}

struct UInt128Traits {
    using Id = UInt128;
    static constexpr std::size_t id_byte_count = 16;

    static bool is_root_parent(UInt128 nodeId) noexcept { return nodeId == 0; }

    static bool equal(UInt128 lhs, UInt128 rhs) noexcept { return lhs == rhs; }

    static uint8_t byte_msb_first(UInt128 nodeId,
                                  std::size_t byteIndex) noexcept {
        const std::size_t shift = (id_byte_count - 1 - byteIndex) * 8U;
        return static_cast<uint8_t>(nodeId >> shift);
    }

    static uint64_t chunk64_msb_first(UInt128 nodeId,
                                      std::size_t chunkIndex) noexcept {
        if (chunkIndex == 0) {
            return static_cast<uint64_t>(nodeId >> 64U);
        }
        return static_cast<uint64_t>(nodeId);
    }

    static uint32_t chunk32_msb_first(UInt128 nodeId,
                                      std::size_t chunkIndex) noexcept {
        const std::size_t shift = (3U - chunkIndex) * 32U;
        return static_cast<uint32_t>(nodeId >> shift);
    }

    template <std::size_t ChunkBytes>
    static auto chunk_msb_first(UInt128 nodeId,
                                std::size_t chunkIndex) noexcept {
        static_assert(
            ChunkBytes == 1 || ChunkBytes == 2 || ChunkBytes == 4 ||
                ChunkBytes == 8,
            "UInt128 chunk_msb_first supports 1, 2, 4, and 8-byte chunks");
        if constexpr (ChunkBytes == 1) {
            return byte_msb_first(nodeId, chunkIndex);
        } else if constexpr (ChunkBytes == 2) {
            const std::size_t shift = (7U - chunkIndex) * 16U;
            return static_cast<uint16_t>(nodeId >> shift);
        } else if constexpr (ChunkBytes == 4) {
            return chunk32_msb_first(nodeId, chunkIndex);
        } else {
            return chunk64_msb_first(nodeId, chunkIndex);
        }
    }

    // Deterministic default hash for the optional UInt128 compatibility type.
    // This hash is used only for internal parent-index placement; canonical
    // ordering is still by depth and MSB-first ID bytes. Applications that
    // accept adversarial IDs should provide their own traits with a keyed or
    // otherwise hardened hash policy.
    static std::size_t hash(UInt128 nodeId) noexcept {
        return detail::hashUint128(nodeId);
    }
};

} // namespace forest_sorting

#endif // FOREST_SORTING_UINT128_HPP
