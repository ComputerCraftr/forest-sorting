#include "app_symbol_positive_fixture.hpp"

namespace forest_sorting::benchmark_app::fixture {

int runFixture(int argc, char **argv) {
    static_cast<void>(argv);
    return app_symbol_fixture::emittedInline(argc) +
           app_symbol_fixture::emittedTemplate(argc);
}

} // namespace forest_sorting::benchmark_app::fixture
