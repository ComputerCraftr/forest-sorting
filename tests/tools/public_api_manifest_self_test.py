#!/usr/bin/env python3

import argparse
import copy
import json
import subprocess
import tempfile
from pathlib import Path

from public_api_manifest import manifest_from_ast
from public_api_manifest_normalize import (
    SourceRepository,
    canonical_expression,
    canonical_type_from_spelling,
    normalized_type,
)


BASE = """
namespace forest_sorting {
template <typename T>
concept Sized = sizeof(T) > 0;
template <typename T> requires Sized<T>
int api(T value) noexcept { return static_cast<int>(value); }
struct Record { int field; void method(int) const noexcept {} };
enum class Choice : unsigned { First, Second };
}
"""

MUTATIONS = {
    "return-type": BASE.replace(
        "int api(T value)", "long api(T value)"
    ).replace("static_cast<int>", "static_cast<long>"),
    "parameter-type": BASE.replace("method(int)", "method(long)"),
    "overload": BASE.replace(
        "void method(int) const noexcept {}",
        "void method(int) const noexcept {}; void method(long) {}",
    ),
    "constraint": BASE.replace("sizeof(T) > 0", "sizeof(T) > 1"),
    "field": BASE.replace("int field", "long field"),
    "method": BASE.replace(
        "void method(int) const noexcept {}",
        "void method(int) const noexcept {}; void added() {}",
    ),
    "member-access": BASE.replace(
        "struct Record { int field; void method(int) const noexcept {} };",
        """class Record {
protected:
  int field;
public:
  void method(int) const noexcept {}
};""",
    ),
    "attribute": BASE.replace(
        "void method(int)", "[[deprecated]] void method(int)"
    ),
    "alias": BASE.replace("int field;", "int field; using Nested = int;"),
    "enumerator": BASE.replace("First, Second", "First, Second, Third"),
    "enumerator-value": BASE.replace("First, Second", "First = 4, Second"),
    "constant-value": BASE.replace(
        "int field;",
        "static constexpr unsigned value = 7; int field;",
    ),
    "default-argument": BASE.replace("method(int)", "method(int value = 7)"),
    "template-default": BASE.replace(
        "template <typename T>", "template <typename T = long>", 1
    ),
    "nested-class-template": BASE.replace(
        "void method(int) const noexcept {}",
        """void method(int) const noexcept {}
  template <typename T> struct Nested { T publicField; };""",
    ),
    "friend": BASE.replace(
        "void method(int) const noexcept {}",
        "void method(int) const noexcept {}; friend void addedFriend(Record) {}",
    ),
    "noexcept": BASE.replace("int api(T value) noexcept", "int api(T value)"),
}


def ast_for(clang, source_text, definitions=()):
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "probe.cpp"
        source.write_text(source_text, encoding="utf-8")
        completed = subprocess.run(
            [
                clang,
                "-std=c++20",
                *definitions,
                "-Xclang",
                "-ast-dump=json",
                "-fsyntax-only",
                str(source),
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        sources = SourceRepository()
        sources.add_text(source, source_text)
        return json.loads(completed.stdout), sources, str(source.resolve())


def manifest_for(clang, source_text, definitions=()):
    ast, sources, source = ast_for(clang, source_text, definitions)
    return manifest_from_ast(ast, sources, "self-test", source)


def assert_no_hash_keys(value):
    if isinstance(value, dict):
        for key, child in value.items():
            lowered = key.lower()
            if "digest" in lowered or "hash" in lowered:
                raise RuntimeError(f"schema contains forbidden key {key}")
            assert_no_hash_keys(child)
    elif isinstance(value, list):
        for child in value:
            assert_no_hash_keys(child)


def rewrite_ast_metadata(value):
    if isinstance(value, dict):
        rewritten = {}
        for key, child in reversed(list(value.items())):
            if key in {"id", "previousDecl"} and isinstance(child, str):
                rewritten[key] = "changed-" + child
            elif key in {"loc", "range"}:
                rewritten[key] = copy.deepcopy(child)
            else:
                rewritten[key] = rewrite_ast_metadata(child)
        return rewritten
    if isinstance(value, list):
        return [rewrite_ast_metadata(child) for child in value]
    return value


def alter_satisfaction_trees(value):
    if isinstance(value, dict):
        if value.get("kind") == "ImplicitConceptSpecializationDecl":
            value["inner"] = [
                {
                    "kind": "TemplateArgument",
                    "value": "compiler-specific-satisfaction",
                }
            ]
            return
        for child in value.values():
            alter_satisfaction_trees(child)
    elif isinstance(value, list):
        for child in value:
            alter_satisfaction_trees(child)


def count_ast_kind(value, kind):
    if isinstance(value, dict):
        return int(value.get("kind") == kind) + sum(
            count_ast_kind(child, kind) for child in value.values()
        )
    if isinstance(value, list):
        return sum(count_ast_kind(child, kind) for child in value)
    return 0


def wrap_constraint_expressions(value):
    if isinstance(value, dict):
        children = value.get("inner")
        if value.get("kind") == "BinaryOperator" and isinstance(children, list):
            value["inner"] = [
                {
                    "kind": "ImplicitCastExpr",
                    "range": copy.deepcopy(child.get("range", value.get("range"))),
                    "inner": [
                        {
                            "kind": "ExprWithCleanups",
                            "range": copy.deepcopy(
                                child.get("range", value.get("range"))
                            ),
                            "inner": [child],
                        }
                    ],
                }
                for child in children
            ]
            return
        if value.get("kind") != "ImplicitConceptSpecializationDecl":
            for child in value.values():
                wrap_constraint_expressions(child)
    elif isinstance(value, list):
        for child in value:
            wrap_constraint_expressions(child)


def assert_type_normalization():
    if canonical_type_from_spelling(
        "std::__1::vector<std::__1::size_t>"
    ) != canonical_type_from_spelling("std::vector<std::size_t>"):
        raise RuntimeError("standard-library inline namespaces were not normalized")
    if canonical_type_from_spelling(
        "std::__cxx11::basic_string<char>"
    ) != canonical_type_from_spelling("std::basic_string<char>"):
        raise RuntimeError("libstdc++ inline namespaces were not normalized")

    libcxx_size = {
        "type": {
            "qualType": "std::__1::size_t",
            "desugaredQualType": "unsigned long",
        }
    }
    libstdcxx_size = {
        "type": {
            "qualType": "std::size_t",
            "desugaredQualType": "unsigned long long",
        }
    }
    if normalized_type(libcxx_size) != normalized_type(libstdcxx_size):
        raise RuntimeError("typedef identity depends on its platform builtin")
    if normalized_type(libcxx_size) == canonical_type_from_spelling(
        "unsigned long"
    ):
        raise RuntimeError("typedef normalization rewrote unsigned long globally")


def assert_unsupported_construct_fails_closed():
    sources = SourceRepository()
    sources.add_text("/tmp/unsupported.cpp", "unsupported")
    try:
        canonical_expression(
            {"kind": "UnsupportedPublicExpression"},
            {},
            sources,
            "/tmp/unsupported.cpp",
            {},
        )
    except RuntimeError as error:
        if "UnsupportedPublicExpression" not in str(error):
            raise
    else:
        raise RuntimeError("unsupported semantic expression did not fail closed")


def assert_ast_representation_normalization(clang):
    ast, sources, source = ast_for(clang, BASE)
    baseline = manifest_from_ast(ast, sources, "self-test", source)

    metadata_ast = rewrite_ast_metadata(ast)
    if manifest_from_ast(metadata_ast, sources, "self-test", source) != baseline:
        raise RuntimeError("AST IDs, property order, or locations changed identity")

    wrapped_ast = copy.deepcopy(ast)
    wrap_constraint_expressions(wrapped_ast)
    if manifest_from_ast(wrapped_ast, sources, "self-test", source) != baseline:
        raise RuntimeError("implicit expression wrappers changed identity")

    satisfaction_ast = copy.deepcopy(ast)
    if (
        count_ast_kind(satisfaction_ast, "ImplicitConceptSpecializationDecl")
        == 0
    ):
        raise RuntimeError("satisfaction-tree fixture contains no concept use")
    alter_satisfaction_trees(satisfaction_ast)
    if (
        manifest_from_ast(satisfaction_ast, sources, "self-test", source)
        != baseline
    ):
        raise RuntimeError("concept satisfaction trees changed identity")

    shifted = manifest_for(clang, "\n// shifted source locations\n" + BASE)
    if shifted != baseline:
        raise RuntimeError("source location changes changed identity")


def assert_logical_normalization(clang):
    left_nested = """
namespace forest_sorting {
template <typename T>
concept Value = (sizeof(T) > 0) && ((sizeof(T) > 1) && (sizeof(T) > 2));
}
"""
    right_nested = left_nested.replace(
        "(sizeof(T) > 0) && ((sizeof(T) > 1) && (sizeof(T) > 2))",
        "((sizeof(T) > 0) && (sizeof(T) > 1)) && (sizeof(T) > 2)",
    )
    if manifest_for(clang, left_nested) != manifest_for(clang, right_nested):
        raise RuntimeError("equivalent logical constraint layouts differ")

    renamed_parameter = left_nested.replace(
        "typename T", "typename ValueType"
    ).replace("sizeof(T)", "sizeof(ValueType)")
    if manifest_for(clang, left_nested) != manifest_for(
        clang, renamed_parameter
    ):
        raise RuntimeError("template parameter spelling changed identity")


def run_self_test(clang):
    baseline = manifest_for(clang, BASE)
    if baseline.get("manifest_schema") != 2:
        raise RuntimeError("manifest self-test did not produce schema 2")
    assert_no_hash_keys(baseline)

    for name, source in MUTATIONS.items():
        if manifest_for(clang, source) == baseline:
            raise RuntimeError(f"manifest self-test missed {name} API mutation")

    conditional_source = """
namespace forest_sorting {
#if FOREST_API_FEATURE
void conditionalApi(int) {}
#endif
}
"""
    disabled = manifest_for(
        clang, conditional_source, ("-DFOREST_API_FEATURE=0",)
    )
    enabled = manifest_for(
        clang, conditional_source, ("-DFOREST_API_FEATURE=1",)
    )
    if disabled == enabled:
        raise RuntimeError(
            "manifest self-test missed a conditional public declaration"
        )

    assert_type_normalization()
    assert_unsupported_construct_fails_closed()
    assert_ast_representation_normalization(clang)
    assert_logical_normalization(clang)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True)
    run_self_test(parser.parse_args().clang)


if __name__ == "__main__":
    main()
