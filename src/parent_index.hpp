#ifndef FOREST_SORTING_PARENT_INDEX_HPP
#define FOREST_SORTING_PARENT_INDEX_HPP

#include "forest.hpp"
#include "radix.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace forest_internal {

struct IdIndexSlot {
    UInt128 id = 0;
    std::size_t nodeIndex = kNoParent;
    bool occupied = false;
};

struct IdIndexEntry {
    UInt128 id;
    std::size_t nodeIndex;
};

struct ParentQueryEntry {
    UInt128 parentId;
    std::size_t childIndex;
};

inline constexpr uint8_t kEmptyControlByte = 0x80;

inline std::size_t nextPowerOfTwo(std::size_t value) noexcept {
    std::size_t capacity = 1;
    while (capacity < value) {
        capacity <<= 1;
    }
    return capacity;
}

class FlatIdIndex {
  public:
    explicit FlatIdIndex(std::size_t itemCount)
        : slots_(nextPowerOfTwo((itemCount * 2) + 1)) {}

    void insert(UInt128 nodeId, std::size_t nodeIndex) {
        const std::size_t mask = slots_.size() - 1;
        std::size_t slotIndex = UInt128Hash{}(nodeId)&mask;
        while (slots_[slotIndex].occupied) {
            if (slots_[slotIndex].id == nodeId) {
                throw std::runtime_error("duplicate node id");
            }
            slotIndex = (slotIndex + 1) & mask;
        }

        slots_[slotIndex] = IdIndexSlot{nodeId, nodeIndex, true};
    }

    std::size_t find(UInt128 nodeId) const noexcept {
        const std::size_t mask = slots_.size() - 1;
        std::size_t slotIndex = UInt128Hash{}(nodeId)&mask;
        while (slots_[slotIndex].occupied) {
            if (slots_[slotIndex].id == nodeId) {
                return slots_[slotIndex].nodeIndex;
            }
            slotIndex = (slotIndex + 1) & mask;
        }

        return kNoParent;
    }

  private:
    std::vector<IdIndexSlot> slots_;
};

class ControlByteFlatIdIndex {
  public:
    explicit ControlByteFlatIdIndex(std::size_t itemCount)
        : control_(nextPowerOfTwo((itemCount * 2) + 1), kEmptyControlByte),
          ids_(control_.size(), 0), nodeIndexes_(control_.size(), kNoParent) {}

    void insert(UInt128 nodeId, std::size_t nodeIndex) {
        const std::size_t hashValue = UInt128Hash{}(nodeId);
        const uint8_t fingerprint = fingerprintForHash(hashValue);
        const std::size_t mask = control_.size() - 1;
        std::size_t slotIndex = hashValue & mask;

        while (control_[slotIndex] != kEmptyControlByte) {
            if (control_[slotIndex] == fingerprint &&
                ids_[slotIndex] == nodeId) {
                throw std::runtime_error("duplicate node id");
            }
            slotIndex = (slotIndex + 1) & mask;
        }

        control_[slotIndex] = fingerprint;
        ids_[slotIndex] = nodeId;
        nodeIndexes_[slotIndex] = nodeIndex;
    }

    std::size_t find(UInt128 nodeId) const noexcept {
        const std::size_t hashValue = UInt128Hash{}(nodeId);
        const uint8_t fingerprint = fingerprintForHash(hashValue);
        const std::size_t mask = control_.size() - 1;
        std::size_t slotIndex = hashValue & mask;

        while (control_[slotIndex] != kEmptyControlByte) {
            if (control_[slotIndex] == fingerprint &&
                ids_[slotIndex] == nodeId) {
                return nodeIndexes_[slotIndex];
            }
            slotIndex = (slotIndex + 1) & mask;
        }

        return kNoParent;
    }

  private:
    static uint8_t fingerprintForHash(std::size_t hashValue) noexcept {
        return static_cast<uint8_t>(hashValue & 0x7FU);
    }

    std::vector<uint8_t> control_;
    std::vector<UInt128> ids_;
    std::vector<std::size_t> nodeIndexes_;
};

inline std::vector<std::size_t>
buildParentIndexFlatHash(const std::vector<Node> &nodes) {
    FlatIdIndex idToIndex(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        idToIndex.insert(nodes[i].id, i);
    }

    std::vector<std::size_t> parent(nodes.size(), kNoParent);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const UInt128 parentId = nodes[i].parentId;
        if (parentId == 0) {
            continue;
        }
        parent[i] = idToIndex.find(parentId);
    }

    return parent;
}

inline std::vector<std::size_t>
buildParentIndexControlByteFlatHash(const std::vector<Node> &nodes) {
    ControlByteFlatIdIndex idToIndex(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        idToIndex.insert(nodes[i].id, i);
    }

    std::vector<std::size_t> parent(nodes.size(), kNoParent);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const UInt128 parentId = nodes[i].parentId;
        if (parentId == 0) {
            continue;
        }
        parent[i] = idToIndex.find(parentId);
    }

    return parent;
}

inline std::vector<std::size_t>
buildParentIndexRadixJoin(const std::vector<Node> &nodes) {
    std::vector<IdIndexEntry> idEntries;
    idEntries.reserve(nodes.size());
    std::vector<ParentQueryEntry> parentQueries;
    parentQueries.reserve(nodes.size());

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        idEntries.push_back(IdIndexEntry{nodes[i].id, i});
        if (nodes[i].parentId != 0) {
            parentQueries.push_back(ParentQueryEntry{nodes[i].parentId, i});
        }
    }

    radixSortEntriesById(idEntries,
                         [](const IdIndexEntry &entry) { return entry.id; });
    radixSortEntriesById(parentQueries, [](const ParentQueryEntry &entry) {
        return entry.parentId;
    });

    for (std::size_t i = 1; i < idEntries.size(); ++i) {
        if (idEntries[i - 1].id == idEntries[i].id) {
            throw std::runtime_error("duplicate node id");
        }
    }

    std::vector<std::size_t> parent(nodes.size(), kNoParent);
    std::size_t idOffset = 0;
    for (const ParentQueryEntry &query : parentQueries) {
        while (idOffset < idEntries.size() &&
               idEntries[idOffset].id < query.parentId) {
            ++idOffset;
        }
        if (idOffset < idEntries.size() &&
            idEntries[idOffset].id == query.parentId) {
            parent[query.childIndex] = idEntries[idOffset].nodeIndex;
        }
    }

    return parent;
}

} // namespace forest_internal

#endif // FOREST_SORTING_PARENT_INDEX_HPP
