#include "forest_sorting/algorithms.hpp"
#include "test_bytes.hpp"

#include <cassert>
#include <cstdint>
#include <exception>
#include <iostream>
#include <vector>

TestBytes<32> makeId(uint64_t high, uint64_t low) {
    return makeTestBytes<32>(static_cast<uint8_t>(high),
                             static_cast<uint8_t>(low));
}

int main() {
    try {
        const std::vector<TestNode<32>> nodes = {
            {makeId(0, 2), makeId(0, 1)},
            {makeId(0, 1), TestBytes<32>{}},
        };

        const auto sorted = forest_sorting::sortedCopyByDepthAndId(
            nodes, TestBytesTraits<32>{});

        assert(sorted[0].id == makeId(0, 1));
        assert(sorted[1].id == makeId(0, 2));
        assert(forest_sorting::verifySortedByDepthAndId(sorted,
                                                        TestBytesTraits<32>{}));
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "portable include test failed: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "portable include test failed: unknown exception\n";
        return 1;
    }
}
