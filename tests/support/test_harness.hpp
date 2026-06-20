#ifndef FOREST_SORTING_SUPPORT_TEST_HARNESS_HPP
#define FOREST_SORTING_SUPPORT_TEST_HARNESS_HPP

#include "forest_sorting/uint128_forest.hpp"

#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace forest_sorting::test_support {

inline void require(bool condition,
                    std::string_view message = "test condition failed") {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

inline void requireNear(double actual, double expected, double tolerance,
                        std::string_view message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(std::string(message));
    }
}

inline void runTest(const char *testName, void (*testFunction)()) {
    std::cout << "RUN  " << testName << "\n";
    testFunction();
    std::cout << "PASS " << testName << "\n";
}

template <typename Depth>
inline void requireSortedByDepthThenId(const std::vector<std::size_t> &order,
                                       const std::vector<Node> &nodes,
                                       const std::vector<Depth> &depths) {
    for (std::size_t nodeIdx = 1; nodeIdx < order.size(); ++nodeIdx) {
        const Depth depth0 = depths[order[nodeIdx - 1]];
        const Depth depth1 = depths[order[nodeIdx]];
        if (depth0 != depth1) {
            require(depth0 < depth1, "depths not in ascending order");
        } else {
            require(nodes[order[nodeIdx - 1]].id < nodes[order[nodeIdx]].id,
                    "ids not in ascending order for same depth");
        }
    }
}

} // namespace forest_sorting::test_support

#endif // FOREST_SORTING_SUPPORT_TEST_HARNESS_HPP
