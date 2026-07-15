#ifndef FOREST_SORTING_SUPPORT_SMALL_SORT_TEST_TYPES_HPP
#define FOREST_SORTING_SUPPORT_SMALL_SORT_TEST_TYPES_HPP

#include "id_dispatch_oracle.hpp"

#include <cstddef>
#include <cstdint>

namespace forest_sorting::test_support {

using CachedScratchTestId = InstrumentedByteId<16>;

struct CachedScratchTestNode {
    CachedScratchTestId id;
};

struct CachedScratchTestTraits {
    using Id = CachedScratchTestId;
    static constexpr std::size_t id_byte_count = 16;

    static const Id &id(const CachedScratchTestNode &node) noexcept {
        return node.id;
    }

    static uint8_t byte_msb_first(const Id &nodeId,
                                  std::size_t byteIndex) noexcept {
        return nodeId.bytes[byteIndex];
    }
};

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_SMALL_SORT_TEST_TYPES_HPP
