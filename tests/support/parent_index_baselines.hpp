#ifndef FOREST_SORTING_SUPPORT_PARENT_INDEX_BASELINES_HPP
#define FOREST_SORTING_SUPPORT_PARENT_INDEX_BASELINES_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/uint128.hpp"
#include "forest_sorting/uint128_forest.hpp"

#include <cstddef>
#include <stdexcept>
#include <unordered_map>
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
        : idTraits_(idTraits),
          slots_(detail::nextPowerOfTwo((itemCount * 2) + 1)) {}

    void insert(const Id &nodeId, std::size_t nodeIndex) {
        const std::size_t mask = slots_.size() - 1;
        std::size_t slotIndex = idTraits_.hash(nodeId) & mask;
        while (slots_[slotIndex].occupied) {
            if (idTraits_.equal(slots_[slotIndex].id, nodeId)) {
                throw std::runtime_error("duplicate node id");
            }
            slotIndex = (slotIndex + 1) & mask;
        }

        slots_[slotIndex] = FlatIdIndexSlot<Id>{nodeId, nodeIndex, true};
    }

    std::size_t find(const Id &nodeId) const noexcept {
        const std::size_t mask = slots_.size() - 1;
        std::size_t slotIndex = idTraits_.hash(nodeId) & mask;
        while (slots_[slotIndex].occupied) {
            if (idTraits_.equal(slots_[slotIndex].id, nodeId)) {
                return slots_[slotIndex].nodeIndex;
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
std::vector<std::size_t>
buildParentIndexFlatHashBaseline(const Nodes &nodes, const Traits &traits) {
    using Id = Traits::Id;
    FlatIdIndex<Id, Traits> idToIndex(nodes.size(), traits);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        idToIndex.insert(traits.id(nodes[nodeIdx]), nodeIdx);
    }

    std::vector<std::size_t> parent(nodes.size(), detail::no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const Id parentId = traits.parent_id(nodes[nodeIdx]);
        if (traits.is_root_parent(parentId)) {
            continue;
        }
        parent[nodeIdx] = idToIndex.find(parentId);
    }

    return parent;
}

struct UInt128BaselineHash {
    std::size_t operator()(UInt128 value) const noexcept {
        return UInt128Traits::hash(value);
    }
};

inline std::vector<std::size_t>
buildParentIndexStdUnorderedMap(const std::vector<Node> &nodes) {
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

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_PARENT_INDEX_BASELINES_HPP
