#ifndef FOREST_SORTING_FOREST_HPP
#define FOREST_SORTING_FOREST_HPP

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using UInt128 = unsigned __int128;

struct Node {
    UInt128 id;
    UInt128 parentId;
};

constexpr std::size_t kNoParent = std::numeric_limits<std::size_t>::max();
constexpr uint32_t kMaxSortableDepth = 1024;

std::string toHex(UInt128 value);

struct UInt128Hash {
    static uint64_t mix64(uint64_t input) noexcept;
    std::size_t operator()(const UInt128 &value) const noexcept;
};

std::vector<std::size_t> buildParentIndex(const std::vector<Node> &nodes);

std::vector<uint32_t>
computeDepths(const std::vector<Node> &nodes,
              const std::vector<std::size_t> &parentIndex);

std::vector<Node> sortForestByDepthAndId(const std::vector<Node> &nodes);

bool verifySortedByDepthAndId(const std::vector<Node> &nodes);

#endif // FOREST_SORTING_FOREST_HPP
