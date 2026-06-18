#ifndef FOREST_SORTING_DETAIL_PARENT_INDEX_HPP
#define FOREST_SORTING_DETAIL_PARENT_INDEX_HPP

#include "forest_sorting/detail/constants.hpp"
#include "forest_sorting/detail/radix.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace forest_sorting::detail {

inline constexpr uint8_t empty_control_byte = 0x80;
inline constexpr std::size_t max_probe_groups_before_fallback = 32;

enum class InsertResult : uint8_t {
    Inserted,
    Duplicate,
    ProbeLimitExceeded,
};

struct FindResult {
    std::size_t nodeIndex = no_parent;
    bool found = false;
    bool probeLimitExceeded = false;
};

template <typename Id> struct RadixJoinIdEntry {
    Id id{};
    std::size_t nodeIndex = no_parent;
};

template <typename Id> struct RadixJoinQueryEntry {
    Id parentId{};
    std::size_t childIndex = no_parent;
};

template <typename Nodes, typename Traits>
std::vector<std::size_t> buildParentIndexRadixJoin(const Nodes &nodes,
                                                   const Traits &traits) {
    using Id = Traits::Id;
    std::vector<RadixJoinIdEntry<Id>> idEntries;
    idEntries.reserve(nodes.size());
    std::vector<RadixJoinQueryEntry<Id>> parentQueries;
    parentQueries.reserve(nodes.size());

    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        idEntries.push_back(
            RadixJoinIdEntry<Id>{traits.id(nodes[nodeIdx]), nodeIdx});
        const Id parentId = traits.parent_id(nodes[nodeIdx]);
        if (!traits.is_root_parent(parentId)) {
            parentQueries.push_back(RadixJoinQueryEntry<Id>{parentId, nodeIdx});
        }
    }

    radixMsdSortEntriesById(
        idEntries, [](const RadixJoinIdEntry<Id> &entry) { return entry.id; },
        traits);
    radixMsdSortEntriesById(
        parentQueries,
        [](const RadixJoinQueryEntry<Id> &entry) { return entry.parentId; },
        traits);

    if (!idEntries.empty()) {
        Id prevId = idEntries[0].id;
        for (std::size_t entryIdx = 1; entryIdx < idEntries.size();
             ++entryIdx) {
            const Id currId = idEntries[entryIdx].id;
            if (traits.equal(prevId, currId)) {
                throw std::runtime_error("duplicate node id");
            }
            prevId = currId;
        }
    }

    std::vector<std::size_t> parent(nodes.size(), no_parent);
    std::size_t idOffset = 0;
    for (const RadixJoinQueryEntry<Id> &query : parentQueries) {
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

template <typename Id, typename IdTraits> class IdIndexTable {
  public:
    IdIndexTable(std::size_t itemCount, const IdTraits &idTraits)
        : idTraits_(idTraits), mask_(nextPowerOfTwo((itemCount * 2) + 1) - 1),
          control_(mask_ + 1 + 8, empty_control_byte), ids_(mask_ + 1),
          nodeIndexes_(mask_ + 1, no_parent) {}

    InsertResult insertBounded(const Id &nodeId, std::size_t nodeIndex,
                               std::size_t maxProbeGroups) {
        const std::size_t hashValue = idTraits_.hash(nodeId);
        const uint8_t fingerprint = fingerprintForHash(hashValue);
        std::size_t slotIndex = hashValue & mask_;

        const uint64_t fp_mask =
            static_cast<uint64_t>(fingerprint) * 0x0101010101010101ULL;
        const uint64_t empty_mask =
            static_cast<uint64_t>(empty_control_byte) * 0x0101010101010101ULL;

        for (std::size_t groupIdx = 0; groupIdx < maxProbeGroups; ++groupIdx) {
            uint64_t group;
            std::memcpy(&group, &control_[slotIndex], 8);

            uint64_t match = group ^ fp_mask;
            uint64_t matchBits = (match - 0x0101010101010101ULL) & ~match &
                                 0x8080808080808080ULL;

            uint64_t empty = group ^ empty_mask;
            uint64_t emptyBits = (empty - 0x0101010101010101ULL) & ~empty &
                                 0x8080808080808080ULL;

            for (uint64_t currentMatches = matchBits; currentMatches != 0;
                 currentMatches &= currentMatches - 1) {
                const int matchIdx = std::countr_zero(currentMatches) >> 3;
                const std::size_t fullIdx =
                    (slotIndex + static_cast<std::size_t>(matchIdx)) & mask_;
                if (idTraits_.equal(ids_[fullIdx], nodeId)) {
                    return InsertResult::Duplicate;
                }
            }

            if (emptyBits != 0) {
                const int emptyIdx = std::countr_zero(emptyBits) >> 3;
                const std::size_t fullIdx =
                    (slotIndex + static_cast<std::size_t>(emptyIdx)) & mask_;
                control_[fullIdx] = fingerprint;
                if (fullIdx < 8) {
                    control_[mask_ + 1 + fullIdx] = fingerprint;
                }
                ids_[fullIdx] = nodeId;
                nodeIndexes_[fullIdx] = nodeIndex;
                return InsertResult::Inserted;
            }

            slotIndex = (slotIndex + 8) & mask_;
        }

        return InsertResult::ProbeLimitExceeded;
    }

    FindResult findBounded(const Id &nodeId,
                           std::size_t maxProbeGroups) const noexcept {
        const std::size_t hashValue = idTraits_.hash(nodeId);
        const uint8_t fingerprint = fingerprintForHash(hashValue);
        std::size_t slotIndex = hashValue & mask_;

        const uint64_t fp_mask =
            static_cast<uint64_t>(fingerprint) * 0x0101010101010101ULL;
        const uint64_t empty_mask =
            static_cast<uint64_t>(empty_control_byte) * 0x0101010101010101ULL;

        for (std::size_t groupIdx = 0; groupIdx < maxProbeGroups; ++groupIdx) {
            uint64_t group;
            std::memcpy(&group, &control_[slotIndex], 8);

            uint64_t match = group ^ fp_mask;
            uint64_t matchBits = (match - 0x0101010101010101ULL) & ~match &
                                 0x8080808080808080ULL;

            uint64_t empty = group ^ empty_mask;
            uint64_t emptyBits = (empty - 0x0101010101010101ULL) & ~empty &
                                 0x8080808080808080ULL;

            for (uint64_t currentMatches = matchBits; currentMatches != 0;
                 currentMatches &= currentMatches - 1) {
                const int matchIdx = std::countr_zero(currentMatches) >> 3;
                const std::size_t fullIdx =
                    (slotIndex + static_cast<std::size_t>(matchIdx)) & mask_;
                if (idTraits_.equal(ids_[fullIdx], nodeId)) {
                    return FindResult{nodeIndexes_[fullIdx], true, false};
                }
            }

            if (emptyBits != 0) {
                return FindResult{no_parent, false, false};
            }
            slotIndex = (slotIndex + 8) & mask_;
        }

        return FindResult{no_parent, false, true};
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
std::vector<std::size_t> buildParentIndex(const Nodes &nodes,
                                          const Traits &traits) {
    using Id = Traits::Id;
    IdIndexTable<Id, Traits> idToIndex(nodes.size(), traits);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const InsertResult insertResult =
            idToIndex.insertBounded(traits.id(nodes[nodeIdx]), nodeIdx,
                                    max_probe_groups_before_fallback);
        if (insertResult == InsertResult::Duplicate) {
            throw std::runtime_error("duplicate node id");
        }
        if (insertResult == InsertResult::ProbeLimitExceeded) {
            return buildParentIndexRadixJoin(nodes, traits);
        }
    }

    std::vector<std::size_t> parent(nodes.size(), no_parent);
    for (std::size_t nodeIdx = 0; nodeIdx < nodes.size(); ++nodeIdx) {
        const Id parentId = traits.parent_id(nodes[nodeIdx]);
        if (traits.is_root_parent(parentId)) {
            continue;
        }
        const FindResult findResult =
            idToIndex.findBounded(parentId, max_probe_groups_before_fallback);
        if (findResult.probeLimitExceeded) {
            return buildParentIndexRadixJoin(nodes, traits);
        }
        if (findResult.found) {
            parent[nodeIdx] = findResult.nodeIndex;
        }
    }

    return parent;
}

} // namespace forest_sorting::detail

#endif // FOREST_SORTING_DETAIL_PARENT_INDEX_HPP
