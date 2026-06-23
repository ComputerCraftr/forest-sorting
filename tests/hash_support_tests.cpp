#include "hash_support.hpp"
#include "test_harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>

using forest_sorting::test_support::require;

#ifdef __SIZEOF_INT128__
using forest_sorting::UInt128;

void test_fnv1a_128_hash() {
    using namespace forest_sorting::test_support;

    const UInt128 val0 = 0;
    const UInt128 val1 = 1;
    const UInt128 val2 = 2;

    const std::size_t hash0 = fnvHashUInt128(val0);
    const std::size_t hash1 = fnvHashUInt128(val1);
    const std::size_t hash2 = fnvHashUInt128(val2);

    require(hash0 != hash1, "hash collision for 0 and 1");
    require(hash1 != hash2, "hash collision for 1 and 2");
    require(hash0 != hash2, "hash collision for 0 and 2");

    // FNV-1a is deterministic
    require(hash0 == fnvHashUInt128(val0), "hash changed for 0");
    require(hash1 == fnvHashUInt128(val1), "hash changed for 1");

    // Test with high bits
    const UInt128 valHigh = static_cast<UInt128>(1) << 100;
    require(fnvHashUInt128(valHigh) != hash0, "high-bit ID collided with zero");
}
#endif

int main() {
    try {
        // 1. Always-compiled FNV portability test
        const std::array<std::uint8_t, 16> bytes = {
            0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
            0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
        };
        const std::uint64_t high = 0x0123456789abcdefULL;
        const std::uint64_t low = 0xfedcba9876543210ULL;

        require(forest_sorting::test_support::fnvHashUInt128Words(high, low) ==
                    forest_sorting::test_support::fnvHashBytes(bytes),
                "portable word and byte hashes differ");

        // 2. Compile-and-run conditional native check when __int128 exists
#ifdef __SIZEOF_INT128__
        const UInt128 native =
            (static_cast<UInt128>(high) << 64U) | static_cast<UInt128>(low);
        require(
            forest_sorting::test_support::fnvHashUInt128(native) ==
                forest_sorting::test_support::fnvHashUInt128Words(high, low),
            "native and portable hashes differ");

        test_fnv1a_128_hash();
#endif

        std::cout << "hash support tests passed successfully\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "hash support tests failed: " << error.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "hash support tests failed: unknown exception\n";
        return 1;
    }
}
