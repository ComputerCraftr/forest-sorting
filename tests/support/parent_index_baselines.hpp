#ifndef FOREST_SORTING_SUPPORT_PARENT_INDEX_BASELINES_HPP
#define FOREST_SORTING_SUPPORT_PARENT_INDEX_BASELINES_HPP

#include "control_parent_index.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/detail/parent_sentinel.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "hash_support.hpp"
#include "uint128_fixtures.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace forest_sorting::test_support {

template <typename Id> struct FlatIdIndexSlot {
    Id id{};
    std::size_t nodeIndex = detail::no_parent;
    bool occupied = false;
};

template <typename Id, typename IdTraits> class FlatIdIndex {
  public:
    FlatIdIndex(std::size_t itemCount, const IdTraits &idTraits)
        : idTraits_(idTraits), slots_(parentIndexTableCapacity(itemCount)) {}

    ControlInsertResult insertBounded(const Id &nodeId, std::size_t nodeIndex,
                                      std::size_t maxProbeLimit) {
        const std::size_t mask = slots_.size() - 1;
        std::size_t slotIndex = idTraits_.hash(nodeId) & mask;
        for (std::size_t probeIdx = 0; probeIdx < maxProbeLimit; ++probeIdx) {
            FlatIdIndexSlot<Id> &slot = slots_[slotIndex];
            if (!slot.occupied) {
                slot = FlatIdIndexSlot<Id>{nodeId, nodeIndex, true};
                return ControlInsertResult::Inserted;
            }
            if (detail::idEqual(slot.id, nodeId, idTraits_)) {
                return ControlInsertResult::Duplicate;
            }
            slotIndex = (slotIndex + 1) & mask;
        }
        return ControlInsertResult::ProbeLimitExceeded;
    }

    ControlFindResult findBounded(const Id &nodeId,
                                  std::size_t maxProbeLimit) const noexcept {
        const std::size_t mask = slots_.size() - 1;
        std::size_t slotIndex = idTraits_.hash(nodeId) & mask;
        for (std::size_t probeIdx = 0; probeIdx < maxProbeLimit; ++probeIdx) {
            const FlatIdIndexSlot<Id> &slot = slots_[slotIndex];
            if (!slot.occupied) {
                return {detail::no_parent, false, false};
            }
            if (detail::idEqual(slot.id, nodeId, idTraits_)) {
                return {slot.nodeIndex, true, false};
            }
            slotIndex = (slotIndex + 1) & mask;
        }
        return {detail::no_parent, false, true};
    }

  private:
    const IdTraits &idTraits_;
    std::vector<FlatIdIndexSlot<Id>> slots_;
};

template <typename Nodes, typename Traits>
std::vector<std::size_t> buildParentIndexFlatHash(const Nodes &nodes,
                                                  const Traits &traits) {
    using Id = Traits::Id;
    FlatIdIndex<Id, Traits> idToIndex(nodes.size(), traits);
    const std::size_t maxProbeLimit = max_probe_groups_before_fallback * 8;
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const ControlInsertResult result = idToIndex.insertBounded(
            traits.id(nodes[nodeIdx]), nodeIdx, maxProbeLimit);
        if (result == ControlInsertResult::Duplicate) {
            throw std::runtime_error("duplicate node id");
        }
        if (result == ControlInsertResult::ProbeLimitExceeded) {
            return detail::buildParentIndexRadixJoinResult(nodes, traits)
                .parentIndex;
        }
    }

    std::vector<std::size_t> parent(nodes.size(), detail::no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const Id parentId = traits.parent_id(nodes[nodeIdx]);
        if (detail::isParentSentinel(traits, parentId)) {
            continue;
        }
        const ControlFindResult result =
            idToIndex.findBounded(parentId, maxProbeLimit);
        if (result.probeLimitExceeded) {
            return detail::buildParentIndexRadixJoinResult(nodes, traits)
                .parentIndex;
        }
        if (result.found) {
            parent[nodeIdx] = result.nodeIndex;
        }
    }

    return parent;
}

struct UInt128BaselineHash {
    std::size_t operator()(UInt128 value) const noexcept {
        return fnvHashUInt128(value);
    }
};

inline std::vector<std::size_t>
buildParentIndexUnorderedMap(const std::vector<Node> &nodes) {
    std::unordered_map<UInt128, std::size_t, UInt128BaselineHash> idToIndex;
    idToIndex.reserve(nodes.size());
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const auto inserted = idToIndex.emplace(nodes[nodeIdx].id, nodeIdx);
        if (!inserted.second) {
            throw std::runtime_error("duplicate node id");
        }
    }

    std::vector<std::size_t> parent(nodes.size(), detail::no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const UInt128 parentId = nodes[nodeIdx].parentId;
        if (parentId == 0) {
            continue;
        }
        const auto parentIt = idToIndex.find(parentId);
        if (parentIt != idToIndex.end()) {
            parent[nodeIdx] = parentIt->second;
        }
    }

    return parent;
}

inline detail::RadixParentIndexResult
buildParentIndexRadixMsdBytePartitionCoreResult(
    const std::vector<Node> &nodes) {
    std::vector<std::size_t> scratch;
    auto sortPermutation = [&](std::vector<std::size_t> &permutation,
                               auto idForIndex, const auto &sortTraits) {
        scratch.resize(permutation.size());
        auto digitForIndex = [&](std::size_t entryIndex,
                                 std::size_t digitIndex) {
            return sortTraits.byte_msb_first(idForIndex(entryIndex),
                                             digitIndex);
        };
        auto rangeDone = [](std::size_t, std::size_t) {};
        detail::radixMsdPartitionRanges(
            permutation, scratch, 0, permutation.size(), 0,
            sortTraits.id_byte_count, digitForIndex, rangeDone);
    };
    return detail::buildParentIndexRadixJoinWithPermutationSorter(
        nodes, UInt128NodeTraits{}, sortPermutation);
}

struct UInt128NodeFinalizerHashTraits : UInt128NodeTraits {
    static std::size_t hash(UInt128 nodeId) noexcept {
        const std::uint64_t highVal = static_cast<uint64_t>(nodeId >> 64);
        const std::uint64_t lowVal = static_cast<uint64_t>(nodeId);

        std::uint64_t mixed = lowVal ^ (highVal * golden_ratio_64);
        mixed = mixDeterministicUInt128Word(mixed);

        if constexpr (sizeof(std::size_t) >= sizeof(std::uint64_t)) {
            return static_cast<std::size_t>(mixed);
        } else {
            return static_cast<std::size_t>(mixed ^ (mixed >> 32));
        }
    }
};

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_PARENT_INDEX_BASELINES_HPP
