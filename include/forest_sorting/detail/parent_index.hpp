#ifndef FOREST_SORTING_DETAIL_PARENT_INDEX_HPP
#define FOREST_SORTING_DETAIL_PARENT_INDEX_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/radix.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace forest_sorting::detail {

inline constexpr uint8_t empty_control_byte = 0x80;

inline std::size_t nextPowerOfTwo(std::size_t value) noexcept {
    std::size_t capacity = 1;
    while (capacity < value) {
        capacity <<= 1;
    }
    return capacity;
}

template <typename Id> struct IdIndexSlot {
    Id id{};
    std::size_t nodeIndex = no_parent;
    bool occupied = false;
};

template <typename Id> struct IdIndexEntry {
    Id id{};
    std::size_t nodeIndex = no_parent;
};

template <typename Id> struct ParentQueryEntry {
    Id parentId{};
    std::size_t childIndex = no_parent;
};

template <typename Id, typename IdTraits> class FlatIdIndex {
  public:
    FlatIdIndex(std::size_t itemCount, const IdTraits &idTraits)
        : idTraits_(idTraits), slots_(nextPowerOfTwo((itemCount * 2) + 1)) {}

    void insert(const Id &nodeId, std::size_t nodeIndex) {
        const std::size_t mask = slots_.size() - 1;
        std::size_t slotIndex = idTraits_.hash(nodeId) & mask;
        while (slots_[slotIndex].occupied) {
            if (idTraits_.equal(slots_[slotIndex].id, nodeId)) {
                throw std::runtime_error("duplicate node id");
            }
            slotIndex = (slotIndex + 1) & mask;
        }

        slots_[slotIndex] = IdIndexSlot<Id>{nodeId, nodeIndex, true};
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

        return no_parent;
    }

  private:
    const IdTraits &idTraits_;
    std::vector<IdIndexSlot<Id>> slots_;
};

template <typename Id, typename IdTraits> class ControlByteFlatIdIndex {
  public:
    ControlByteFlatIdIndex(std::size_t itemCount, const IdTraits &idTraits)
        : idTraits_(idTraits), mask_(nextPowerOfTwo((itemCount * 2) + 1) - 1),
          control_(mask_ + 1 + 8, empty_control_byte), ids_(mask_ + 1),
          nodeIndexes_(mask_ + 1, no_parent) {}

    void insert(const Id &nodeId, std::size_t nodeIndex) {
        const std::size_t hashValue = idTraits_.hash(nodeId);
        const uint8_t fingerprint = fingerprintForHash(hashValue);
        std::size_t slotIndex = hashValue & mask_;

        // NOLINTNEXTLINE(bugprone-infinite-loop)
        while (true) {
            for (std::size_t offset = 0; offset < 8; ++offset) {
                const std::size_t idx = (slotIndex + offset) & mask_;
                const uint8_t ctrl = control_[idx];
                if (ctrl == empty_control_byte) {
                    control_[idx] = fingerprint;
                    if (idx < 8) {
                        control_[mask_ + 1 + idx] = fingerprint;
                    }
                    ids_[idx] = nodeId;
                    nodeIndexes_[idx] = nodeIndex;
                    return;
                }
                if (ctrl == fingerprint && idTraits_.equal(ids_[idx], nodeId)) {
                    throw std::runtime_error("duplicate node id");
                }
            }
            slotIndex = (slotIndex + 8) & mask_;
        }
    }

    std::size_t find(const Id &nodeId) const noexcept {
        const std::size_t hashValue = idTraits_.hash(nodeId);
        const uint8_t fingerprint = fingerprintForHash(hashValue);
        std::size_t slotIndex = hashValue & mask_;

        const uint64_t fp_mask =
            static_cast<uint64_t>(fingerprint) * 0x0101010101010101ULL;
        const uint64_t empty_mask =
            static_cast<uint64_t>(empty_control_byte) * 0x0101010101010101ULL;

        // NOLINTNEXTLINE(bugprone-infinite-loop)
        while (true) {
            uint64_t group;
            std::memcpy(&group, &control_[slotIndex], 8);

            uint64_t match = group ^ fp_mask;
            uint64_t matchBits = (match - 0x0101010101010101ULL) & ~match &
                                 0x8080808080808080ULL;

            uint64_t empty = group ^ empty_mask;
            uint64_t emptyBits = (empty - 0x0101010101010101ULL) & ~empty &
                                 0x8080808080808080ULL;

            uint64_t currentMatches = matchBits;
            // NOLINTNEXTLINE(bugprone-infinite-loop)
            while (currentMatches != 0) {
                const int matchIdx = __builtin_ctzll(currentMatches) >> 3;
                const std::size_t idx =
                    (slotIndex + static_cast<std::size_t>(matchIdx)) & mask_;
                if (idTraits_.equal(ids_[idx], nodeId)) {
                    return nodeIndexes_[idx];
                }
                const uint64_t nextMatches =
                    currentMatches & (currentMatches - 1);
                currentMatches = nextMatches;
            }

            if (emptyBits != 0) {
                return no_parent;
            }
            slotIndex = (slotIndex + 8) & mask_;
        }
    }

  private:
    static uint8_t fingerprintForHash(std::size_t hashValue) noexcept {
        return static_cast<uint8_t>(hashValue & 0x7FU);
    }

    const IdTraits &idTraits_;
    const std::size_t mask_;
    std::vector<uint8_t> control_;
    std::vector<Id> ids_;
    std::vector<std::size_t> nodeIndexes_;
};

template <typename Nodes, typename Traits>
std::vector<std::size_t>
buildParentIndexControlByteFlatHash(const Nodes &nodes, const Traits &traits) {
    using Id = Traits::Id;
    ControlByteFlatIdIndex<Id, Traits> idToIndex(nodes.size(), traits);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        idToIndex.insert(traits.id(nodes[nodeIdx]), nodeIdx);
    }

    std::vector<std::size_t> parent(nodes.size(), no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const Id parentId = traits.parent_id(nodes[nodeIdx]);
        if (traits.is_root_parent(parentId)) {
            continue;
        }
        parent[nodeIdx] = idToIndex.find(parentId);
    }

    return parent;
}

template <typename Nodes, typename Traits>
std::vector<std::size_t> buildParentIndexFlatHash(const Nodes &nodes,
                                                  const Traits &traits) {
    using Id = Traits::Id;
    FlatIdIndex<Id, Traits> idToIndex(nodes.size(), traits);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        idToIndex.insert(traits.id(nodes[nodeIdx]), nodeIdx);
    }

    std::vector<std::size_t> parent(nodes.size(), no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const Id parentId = traits.parent_id(nodes[nodeIdx]);
        if (traits.is_root_parent(parentId)) {
            continue;
        }
        parent[nodeIdx] = idToIndex.find(parentId);
    }

    return parent;
}

template <typename Nodes, typename Traits>
std::vector<std::size_t> buildParentIndexRadixJoin(const Nodes &nodes,
                                                   const Traits &traits) {
    using Id = Traits::Id;
    std::vector<IdIndexEntry<Id>> idEntries;
    idEntries.reserve(nodes.size());
    std::vector<ParentQueryEntry<Id>> parentQueries;
    parentQueries.reserve(nodes.size());

    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        idEntries.push_back(
            IdIndexEntry<Id>{traits.id(nodes[nodeIdx]), nodeIdx});
        const Id parentId = traits.parent_id(nodes[nodeIdx]);
        if (!traits.is_root_parent(parentId)) {
            parentQueries.push_back(ParentQueryEntry<Id>{parentId, nodeIdx});
        }
    }

    radixSortEntriesById(
        idEntries, [](const IdIndexEntry<Id> &entry) { return entry.id; },
        traits);
    radixSortEntriesById(
        parentQueries,
        [](const ParentQueryEntry<Id> &entry) { return entry.parentId; },
        traits);

    for (std::size_t entryIdx = 1; entryIdx < idEntries.size(); ++entryIdx) {
        if (traits.equal(idEntries[entryIdx - 1].id, idEntries[entryIdx].id)) {
            throw std::runtime_error("duplicate node id");
        }
    }

    std::vector<std::size_t> parent(nodes.size(), no_parent);
    std::size_t idOffset = 0;
    for (const ParentQueryEntry<Id> &query : parentQueries) {
        while (idOffset < idEntries.size() &&
               idLess(idEntries[idOffset].id, query.parentId, traits)) {
            ++idOffset;
        }
        if (idOffset < idEntries.size() &&
            traits.equal(idEntries[idOffset].id, query.parentId)) {
            parent[query.childIndex] = idEntries[idOffset].nodeIndex;
        }
    }

    return parent;
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_PARENT_INDEX_HPP
