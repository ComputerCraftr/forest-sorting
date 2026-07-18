#ifndef FOREST_SORTING_UINT128_HPP
#define FOREST_SORTING_UINT128_HPP

#ifndef __SIZEOF_INT128__
#error                                                                         \
    "forest_sorting UInt128 support requires unsigned __int128. Include forest_sorting/algorithms.hpp and use caller-owned ID types with custom traits for portable code."
#endif

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <sstream>
#include <string>

namespace forest_sorting {

__extension__ using UInt128 = unsigned __int128;

inline UInt128 makeId(uint64_t high, uint64_t low) noexcept {
    return (static_cast<UInt128>(high) << 64) | static_cast<UInt128>(low);
}

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

    static bool is_parent_sentinel(UInt128 nodeId) noexcept {
        return nodeId == 0;
    }

    static uint8_t byte_msb_first(UInt128 nodeId,
                                  std::size_t byteIndex) noexcept {
        const std::size_t shift = (id_byte_count - 1 - byteIndex) * 8;
        return static_cast<uint8_t>(nodeId >> shift);
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
            const std::size_t shift = (7 - chunkIndex) * 16;
            return static_cast<uint16_t>(nodeId >> shift);
        } else if constexpr (ChunkBytes == 4) {
            const std::size_t shift = (3 - chunkIndex) * 32;
            return static_cast<uint32_t>(nodeId >> shift);
        } else {
            if (chunkIndex == 0) {
                return static_cast<uint64_t>(nodeId >> 64);
            }
            return static_cast<uint64_t>(nodeId);
        }
    }
};

} // namespace forest_sorting

#endif // FOREST_SORTING_UINT128_HPP
