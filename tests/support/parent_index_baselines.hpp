#ifndef FOREST_SORTING_SUPPORT_PARENT_INDEX_BASELINES_HPP
#define FOREST_SORTING_SUPPORT_PARENT_INDEX_BASELINES_HPP

#include "control_parent_index.hpp"
#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/id_compare.hpp"
#include "forest_sorting/detail/parent_index.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"
#include "hash_support.hpp"
#include "uint128_fixtures.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace forest_sorting::test_support {

struct ParentBuildArtifacts {
    std::vector<std::size_t> parentIndex;
    std::vector<std::size_t> idPermutation;
    bool hasIdPermutation = false;
};

enum class ParentKind : uint8_t {
    Unordered,
    Flat,
    Control,
    ControlFinalizerHash,
    Radix,
    RadixByteMsd,
};

inline constexpr std::array<ParentKind, 1> kDefaultParentKinds = {
    ParentKind::Radix,
};

constexpr std::array<ParentKind, 1> defaultParentKinds() noexcept {
    return kDefaultParentKinds;
}

inline constexpr std::array<ParentKind, 6> kRegisteredParentKinds = {
    ParentKind::Unordered, ParentKind::Flat,
    ParentKind::Control,   ParentKind::ControlFinalizerHash,
    ParentKind::Radix,     ParentKind::RadixByteMsd,
};

constexpr std::array<ParentKind, 6> registeredParentKinds() noexcept {
    return kRegisteredParentKinds;
}

inline std::string_view parentName(ParentKind parentKind) {
    switch (parentKind) {
    case ParentKind::Unordered:
        return "unordered";
    case ParentKind::Flat:
        return "flat";
    case ParentKind::Control:
        return "control";
    case ParentKind::ControlFinalizerHash:
        return "control-finalizer-hash";
    case ParentKind::Radix:
        return "radix";
    case ParentKind::RadixByteMsd:
        return "radix-byte-msd";
    }
    return "unknown";
}

template <typename Id> struct FlatIdIndexSlot {
    Id id{};
    std::size_t nodeIndex = detail::no_parent;
    bool occupied = false;
};

template <typename Id, typename IdTraits> class FlatIdIndex {
  public:
    FlatIdIndex(std::size_t itemCount, const IdTraits &idTraits)
        : idTraits_(idTraits),
          slots_(detail::nextPowerOfTwo((itemCount * 2) + 1)) {}

    bool insert(const Id &nodeId, std::size_t nodeIndex) {
        const std::size_t mask = slots_.size() - 1;
        std::size_t slotIndex = idTraits_.hash(nodeId) & mask;
        for (;;) {
            FlatIdIndexSlot<Id> &slot = slots_[slotIndex];
            if (!slot.occupied) {
                slot = FlatIdIndexSlot<Id>{nodeId, nodeIndex, true};
                return true;
            }
            if (detail::idEqual(slot.id, nodeId, idTraits_)) {
                return false;
            }
            slotIndex = (slotIndex + 1) & mask;
        }
    }

    std::size_t find(const Id &nodeId) const noexcept {
        const std::size_t mask = slots_.size() - 1;
        std::size_t slotIndex = idTraits_.hash(nodeId) & mask;
        for (;;) {
            const FlatIdIndexSlot<Id> &slot = slots_[slotIndex];
            if (!slot.occupied) {
                break;
            }
            if (detail::idEqual(slot.id, nodeId, idTraits_)) {
                return slot.nodeIndex;
            }
            slotIndex = (slotIndex + 1) & mask;
        }

        return detail::no_parent;
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
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        if (!idToIndex.insert(traits.id(nodes[nodeIdx]), nodeIdx)) {
            throw std::runtime_error("duplicate node id");
        }
    }

    std::vector<std::size_t> parent(nodes.size(), detail::no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const Id parentId = traits.parent_id(nodes[nodeIdx]);
        if (detail::isParentSentinel(traits, parentId)) {
            continue;
        }
        parent[nodeIdx] = idToIndex.find(parentId);
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
    idToIndex.reserve(nodes.size() * 2);
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
buildParentIndexRadixByteMsdResult(const std::vector<Node> &nodes) {
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

inline ParentBuildArtifacts
buildParentArtifactsForKind(ParentKind parentKind,
                            const std::vector<Node> &nodes) {
    switch (parentKind) {
    case ParentKind::Unordered:
        return {buildParentIndexUnorderedMap(nodes), {}, false};
    case ParentKind::Flat:
        return {buildParentIndexFlatHash(nodes, UInt128NodeHashedTraits{}),
                {},
                false};
    case ParentKind::Control:
        return {buildParentIndexControl(nodes, UInt128NodeHashedTraits{}),
                {},
                false};
    case ParentKind::ControlFinalizerHash:
        return {
            buildParentIndexControl(nodes, UInt128NodeFinalizerHashTraits{}),
            {},
            false};
    case ParentKind::Radix: {
        auto result =
            detail::buildParentIndexRadixJoinResult(nodes, UInt128NodeTraits{});
        return {std::move(result.parentIndex), std::move(result.idPermutation),
                true};
    }
    case ParentKind::RadixByteMsd: {
        auto result = buildParentIndexRadixByteMsdResult(nodes);
        return {std::move(result.parentIndex), std::move(result.idPermutation),
                true};
    }
    }
    throw std::runtime_error("unknown parent builder");
}

inline std::vector<std::size_t>
buildParentIndexForKind(ParentKind parentKind, const std::vector<Node> &nodes) {
    auto artifacts = buildParentArtifactsForKind(parentKind, nodes);
    return std::move(artifacts.parentIndex);
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_PARENT_INDEX_BASELINES_HPP
