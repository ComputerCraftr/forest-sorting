void accidentalGlobal();

namespace forest_sorting {

void accidentalExport();

namespace benchmark_support {

void accidentalExport();

} // namespace benchmark_support

struct AccidentalMember {
    static void method();
    static int value;
};

} // namespace forest_sorting

template <typename T> void accidentalFunctionTemplate() {}

template <typename T> int accidentalVariableTemplate = 0;

template <typename T> struct AccidentalClassTemplate {
    static int value;
    void method() {}
};

template <typename T> int AccidentalClassTemplate<T>::value = 0;

void accidentalGlobal() {}

void forest_sorting::accidentalExport() {}

void forest_sorting::benchmark_support::accidentalExport() {}

void forest_sorting::AccidentalMember::method() {}

int forest_sorting::AccidentalMember::value = 0;

template void accidentalFunctionTemplate<int>();
template int accidentalVariableTemplate<int>;
template struct AccidentalClassTemplate<int>;
