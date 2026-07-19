#ifndef FOREST_SORTING_TESTS_APP_SYMBOL_POSITIVE_FIXTURE_HPP
#define FOREST_SORTING_TESTS_APP_SYMBOL_POSITIVE_FIXTURE_HPP

#ifdef _MSC_VER
#define FOREST_SORTING_NOINLINE __declspec(noinline)
#else
#define FOREST_SORTING_NOINLINE __attribute__((noinline))
#endif

namespace forest_sorting::app_symbol_fixture {

inline FOREST_SORTING_NOINLINE int emittedInline(int value) {
    return value + 1;
}

template <typename T> FOREST_SORTING_NOINLINE T emittedTemplate(T value) {
    return value + T{1};
}

} // namespace forest_sorting::app_symbol_fixture

#undef FOREST_SORTING_NOINLINE

#endif // FOREST_SORTING_TESTS_APP_SYMBOL_POSITIVE_FIXTURE_HPP
