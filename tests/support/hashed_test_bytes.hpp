#ifndef FOREST_SORTING_SUPPORT_HASHED_TEST_BYTES_HPP
#define FOREST_SORTING_SUPPORT_HASHED_TEST_BYTES_HPP

#include "hash_support.hpp"
#include "test_bytes.hpp"

#include <cstddef>

template <std::size_t ByteCount>
struct TestBytesTraits : HashFreeTestBytesTraits<ByteCount> {
    using Id = TestBytes<ByteCount>;

    std::size_t hash(const Id &nodeId) const noexcept {
        return forest_sorting::test_support::fnvHashBytes(nodeId.bytes);
    }
};

#endif // FOREST_SORTING_SUPPORT_HASHED_TEST_BYTES_HPP
