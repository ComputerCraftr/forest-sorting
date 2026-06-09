#include "forest.hpp"

#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <vector>

UInt128 makeId(uint64_t high, uint64_t low) {
    return (static_cast<UInt128>(high) << 64) | static_cast<UInt128>(low);
}

void test_compute_depths_simple_chain() {
    std::vector<Node> nodes = {
        {makeId(0, 1), 0},            // depth 0
        {makeId(0, 2), makeId(0, 1)}, // depth 1
        {makeId(0, 3), makeId(0, 2)}, // depth 2
    };

    const auto parentIndex = buildParentIndex(nodes);
    const auto depths = computeDepths(nodes, parentIndex);

    assert(depths.size() == 3);
    assert(depths[0] == 0);
    assert(depths[1] == 1);
    assert(depths[2] == 2);
}

void test_sort_and_verify_multi_root() {
    std::vector<Node> nodes = {
        {makeId(0, 10), 0}, // root A
        {makeId(0, 20), 0}, // root B
        {makeId(0, 11), makeId(0, 10)},
        {makeId(0, 12), makeId(0, 10)},
        {makeId(0, 21), makeId(0, 20)},
    };

    auto sorted = sortForestByDepthAndId(nodes);
    assert(verifySortedByDepthAndId(sorted));

    // Roots should be in id order.
    assert(sorted[0].id == makeId(0, 10));
    assert(sorted[1].id == makeId(0, 20));

    // Depth-1 nodes from the first root should come before depth-1 nodes of the
    // second root.
    assert(sorted[2].id == makeId(0, 11));
    assert(sorted[3].id == makeId(0, 12));
    assert(sorted[4].id == makeId(0, 21));
}

int main() {
    try {
        test_compute_depths_simple_chain();
        test_sort_and_verify_multi_root();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "forest-sorting-tests failed: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "forest-sorting-tests failed: unknown exception\n";
        return 1;
    }
}
