#ifndef FOREST_SORTING_SUPPORT_CONTROL_PARENT_INDEX_HPP
#define FOREST_SORTING_SUPPORT_CONTROL_PARENT_INDEX_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/detail/parent_sentinel.hpp"

#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace forest_sorting::test_support {

inline constexpr uint8_t empty_control_byte = 0x80;
inline constexpr std::size_t max_probe_groups_before_fallback = 32;

template <typename Traits, typename Id>
concept HasTraitsHash = requires(const Traits &traits, const Id &nodeId) {
    { traits.hash(nodeId) } -> std::convertible_to<std::size_t>;
};

enum class ControlInsertResult : uint8_t {
    Inserted,
    Duplicate,
    ProbeLimitExceeded,
};

struct ControlFindResult {
    std::size_t nodeIndex = detail::no_parent;
    bool found = false;
    bool probeLimitExceeded = false;
};

template <typename Id, typename Traits>
    requires HasTraitsHash<Traits, Id>
class ControlIdIndexTable {
  public:
    ControlIdIndexTable(std::size_t itemCount, const Traits &traits)
        : traits_(traits),
          mask_(detail::nextPowerOfTwo((itemCount * 2) + 1) - 1),
          control_(mask_ + 1 + 8, empty_control_byte), ids_(mask_ + 1),
          nodeIndexes_(mask_ + 1, detail::no_parent) {}

    ControlInsertResult insertBounded(const Id &nodeId, std::size_t nodeIndex,
                                      std::size_t maxProbeGroups) {
        const std::size_t hashValue = traits_.hash(nodeId);
        const uint8_t fingerprint = fingerprintForHash(hashValue);
        std::size_t slotIndex = hashValue & mask_;
        const uint64_t fingerprintMask =
            static_cast<uint64_t>(fingerprint) * 0x0101010101010101ULL;
        const uint64_t emptyMask =
            static_cast<uint64_t>(empty_control_byte) * 0x0101010101010101ULL;

        for (std::size_t groupIndex = 0; groupIndex < maxProbeGroups;
             ++groupIndex) {
            uint64_t group = 0;
            std::memcpy(&group, &control_[slotIndex], sizeof(group));
            uint64_t match = group ^ fingerprintMask;
            uint64_t matchBits = (match - 0x0101010101010101ULL) & ~match &
                                 0x8080808080808080ULL;
            const uint64_t empty = group ^ emptyMask;
            const uint64_t emptyBits = (empty - 0x0101010101010101ULL) &
                                       ~empty & 0x8080808080808080ULL;

            for (; matchBits != 0; matchBits &= matchBits - 1) {
                const int matchIndex = std::countr_zero(matchBits) >> 3;
                const std::size_t fullIndex =
                    (slotIndex + static_cast<std::size_t>(matchIndex)) & mask_;
                if (detail::idEqual(ids_[fullIndex], nodeId, traits_)) {
                    return ControlInsertResult::Duplicate;
                }
            }

            if (emptyBits != 0) {
                const int emptyIndex = std::countr_zero(emptyBits) >> 3;
                const std::size_t fullIndex =
                    (slotIndex + static_cast<std::size_t>(emptyIndex)) & mask_;
                control_[fullIndex] = fingerprint;
                if (fullIndex < 8) {
                    control_[mask_ + 1 + fullIndex] = fingerprint;
                }
                ids_[fullIndex] = nodeId;
                nodeIndexes_[fullIndex] = nodeIndex;
                return ControlInsertResult::Inserted;
            }
            slotIndex = (slotIndex + 8) & mask_;
        }
        return ControlInsertResult::ProbeLimitExceeded;
    }

    ControlFindResult findBounded(const Id &nodeId,
                                  std::size_t maxProbeGroups) const noexcept {
        const std::size_t hashValue = traits_.hash(nodeId);
        const uint8_t fingerprint = fingerprintForHash(hashValue);
        std::size_t slotIndex = hashValue & mask_;
        const uint64_t fingerprintMask =
            static_cast<uint64_t>(fingerprint) * 0x0101010101010101ULL;
        const uint64_t emptyMask =
            static_cast<uint64_t>(empty_control_byte) * 0x0101010101010101ULL;

        for (std::size_t groupIndex = 0; groupIndex < maxProbeGroups;
             ++groupIndex) {
            uint64_t group = 0;
            std::memcpy(&group, &control_[slotIndex], sizeof(group));
            uint64_t match = group ^ fingerprintMask;
            uint64_t matchBits = (match - 0x0101010101010101ULL) & ~match &
                                 0x8080808080808080ULL;
            const uint64_t empty = group ^ emptyMask;
            const uint64_t emptyBits = (empty - 0x0101010101010101ULL) &
                                       ~empty & 0x8080808080808080ULL;

            for (; matchBits != 0; matchBits &= matchBits - 1) {
                const int matchIndex = std::countr_zero(matchBits) >> 3;
                const std::size_t fullIndex =
                    (slotIndex + static_cast<std::size_t>(matchIndex)) & mask_;
                if (detail::idEqual(ids_[fullIndex], nodeId, traits_)) {
                    return {nodeIndexes_[fullIndex], true, false};
                }
            }
            if (emptyBits != 0) {
                return {detail::no_parent, false, false};
            }
            slotIndex = (slotIndex + 8) & mask_;
        }
        return {detail::no_parent, false, true};
    }

  private:
    static uint8_t fingerprintForHash(std::size_t hashValue) noexcept {
        return static_cast<uint8_t>(hashValue & 0x7FU);
    }

    const Traits &traits_;
    const std::size_t mask_;
    std::vector<uint8_t> control_;
    std::vector<Id> ids_;
    std::vector<std::size_t> nodeIndexes_;
};

template <typename Nodes, typename Traits>
    requires HasTraitsHash<Traits, typename Traits::Id>
std::vector<std::size_t> buildParentIndexControl(const Nodes &nodes,
                                                 const Traits &traits) {
    using Id = Traits::Id;
    ControlIdIndexTable<Id, Traits> idToIndex(nodes.size(), traits);
    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        const ControlInsertResult result =
            idToIndex.insertBounded(traits.id(nodes[nodeIndex]), nodeIndex,
                                    max_probe_groups_before_fallback);
        if (result == ControlInsertResult::Duplicate) {
            throw std::runtime_error("duplicate node id");
        }
        if (result == ControlInsertResult::ProbeLimitExceeded) {
            return detail::buildParentIndexRadixJoinResult(nodes, traits)
                .parentIndex;
        }
    }

    std::vector<std::size_t> parentIndex(nodes.size(), detail::no_parent);
    for (std::size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex) {
        const Id parentId = traits.parent_id(nodes[nodeIndex]);
        if (detail::isParentSentinel(traits, parentId)) {
            continue;
        }
        const ControlFindResult result =
            idToIndex.findBounded(parentId, max_probe_groups_before_fallback);
        if (result.probeLimitExceeded) {
            return detail::buildParentIndexRadixJoinResult(nodes, traits)
                .parentIndex;
        }
        if (result.found) {
            parentIndex[nodeIndex] = result.nodeIndex;
        }
    }
    return parentIndex;
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_CONTROL_PARENT_INDEX_HPP
