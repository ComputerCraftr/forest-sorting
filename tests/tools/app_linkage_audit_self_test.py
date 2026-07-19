#!/usr/bin/env python3

import tempfile
from pathlib import Path

from app_linkage_audit import (
    DeclarationIndex,
    INCLUDED_FILE_ORIGIN,
    audit,
    is_approved_local_descendant,
    linkage_classification,
    resolve_origin,
)


def function(node_id, name, location=None, storage=None):
    node = {
        "id": node_id,
        "kind": "FunctionDecl",
        "name": name,
        "type": {"qualType": "void ()"},
        "inner": [{"kind": "CompoundStmt"}],
    }
    if location is not None:
        node["loc"] = location
    if storage is not None:
        node["storageClass"] = storage
    return node


def test_missing_origin_fails_closed():
    ast = {
        "id": "tu",
        "kind": "TranslationUnitDecl",
        "inner": [function("missing", "missingOrigin")],
    }
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "source.cpp"
        source.write_text("\n", encoding="utf-8")
        report = audit(ast, source, "expectedRunner")
    failure = next(
        item for item in report["failures"] if item["name"] == "missingOrigin"
    )
    assert failure["status"] == "unclassifiable"


def test_previous_declaration_resolves_origin():
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "source.cpp"
        source.write_text("void exported();\nvoid exported() {}\n", encoding="utf-8")
        declaration = {
            "id": "decl",
            "kind": "FunctionDecl",
            "name": "exported",
            "loc": {
                "file": str(source),
                "line": 1,
                "col": 6,
                "offset": 5,
                "tokLen": 8,
            },
        }
        definition = function("definition", "exported")
        definition["previousDecl"] = "decl"
        ast = {
            "id": "tu",
            "kind": "TranslationUnitDecl",
            "inner": [definition, declaration],
        }
        index = DeclarationIndex(ast)
        origin, chain = resolve_origin(definition, index)
    assert origin == str(source.resolve())
    assert chain[-1]["relation"] == "previousDecl"


def test_elided_included_file_is_not_unclassifiable():
    definition = function(
        "header-definition",
        "headerDefinition",
        {
            "line": 10,
            "col": 3,
            "includedFrom": {"file": "/project/source.cpp"},
        },
    )
    ast = {
        "id": "tu",
        "kind": "TranslationUnitDecl",
        "inner": [definition],
    }
    origin, chain = resolve_origin(definition, DeclarationIndex(ast))
    assert origin == INCLUDED_FILE_ORIGIN
    assert chain[0]["included_from"].endswith("/project/source.cpp")


def test_template_instantiation_pattern_resolves_origin():
    pattern = function(
        "pattern",
        "operator()",
        {"file": "/system/header.hpp", "line": 8, "col": 5},
    )
    instantiation = function("instantiation", "operator()")
    instantiation["TemplateInstantiationPattern"] = "pattern"
    ast = {
        "id": "tu",
        "kind": "TranslationUnitDecl",
        "inner": [instantiation, pattern],
    }
    origin, chain = resolve_origin(instantiation, DeclarationIndex(ast))
    assert origin.endswith("/system/header.hpp")
    assert chain[-1]["relation"] == "TemplateInstantiationPattern"


def test_static_member_is_external():
    method = {
        "id": "method",
        "kind": "CXXMethodDecl",
        "name": "method",
        "storageClass": "static",
        "parentDeclContextId": "record",
        "inner": [{"kind": "CompoundStmt"}],
    }
    record = {
        "id": "record",
        "kind": "CXXRecordDecl",
        "name": "Owner",
        "inner": [method],
    }
    ast = {
        "id": "tu",
        "kind": "TranslationUnitDecl",
        "inner": [record],
    }
    index = DeclarationIndex(ast)
    assert linkage_classification(method, index)[0] == "external"


def test_macro_expansion_origin_precedes_spelling():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root / "source.cpp"
        header = root / "header.hpp"
        source.write_text("MACRO()\n", encoding="utf-8")
        header.write_text("#define MACRO()\n", encoding="utf-8")
        definition = function(
            "definition",
            "expanded",
            {
                "expansionLoc": {"file": str(source), "line": 1, "col": 1},
                "spellingLoc": {"file": str(header), "line": 1, "col": 9},
            },
        )
        ast = {
            "id": "tu",
            "kind": "TranslationUnitDecl",
            "inner": [definition],
        }
        origin, chain = resolve_origin(definition, DeclarationIndex(ast))
    assert origin == str(source.resolve())
    assert chain[0]["location_kind"] == "macro-expansion"


def test_namespace_static_is_internal():
    static_function = function("function", "helper", storage="static")
    ast = {
        "id": "tu",
        "kind": "TranslationUnitDecl",
        "inner": [static_function],
    }
    index = DeclarationIndex(ast)
    assert linkage_classification(static_function, index)[0] == "internal"


def test_local_class_descendant_requires_approved_enclosing_function():
    method = {
        "id": "method",
        "kind": "CXXMethodDecl",
        "name": "method",
        "inner": [{"id": "method-body", "kind": "CompoundStmt"}],
    }
    record = {
        "id": "record",
        "kind": "CXXRecordDecl",
        "name": "Local",
        "inner": [method],
    }
    runner = {
        "id": "runner",
        "kind": "FunctionDecl",
        "name": "run",
        "type": {"qualType": "int (int, char **)"},
        "inner": [
            {
                "id": "runner-body",
                "kind": "CompoundStmt",
                "inner": [record],
            }
        ],
    }
    namespace = {
        "id": "namespace",
        "kind": "NamespaceDecl",
        "name": "app",
        "inner": [runner],
    }
    ast = {
        "id": "tu",
        "kind": "TranslationUnitDecl",
        "inner": [namespace],
    }
    index = DeclarationIndex(ast)
    assert is_approved_local_descendant(method, index, "app::run")
    assert not is_approved_local_descendant(method, index, "app::other")


def main():
    test_missing_origin_fails_closed()
    test_previous_declaration_resolves_origin()
    test_elided_included_file_is_not_unclassifiable()
    test_template_instantiation_pattern_resolves_origin()
    test_static_member_is_external()
    test_macro_expansion_origin_precedes_spelling()
    test_namespace_static_is_internal()
    test_local_class_descendant_requires_approved_enclosing_function()


if __name__ == "__main__":
    main()
