#!/usr/bin/env python3

import argparse
import functools
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from clang_ast import load_ast, verify_manifest_clang

FUNCTION_KINDS = {
    "CXXConstructorDecl",
    "CXXConversionDecl",
    "CXXDestructorDecl",
    "CXXMethodDecl",
    "FunctionDecl",
}
VARIABLE_KINDS = {"VarDecl"}
INSTANTIATION_KINDS = {"ClassTemplateSpecializationDecl"}
CONTEXT_KINDS = {
    "ClassTemplateDecl",
    "ClassTemplateSpecializationDecl",
    "CXXRecordDecl",
    "FunctionDecl",
    "FunctionTemplateDecl",
    "NamespaceDecl",
    "RecordDecl",
    "TranslationUnitDecl",
    "VarTemplateDecl",
}
BODY_KINDS = {"CompoundStmt", "CXXTryStmt"}
INCLUDED_FILE_ORIGIN = "<included-file-with-elided-path>"


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True)
    parser.add_argument("--compile-commands", required=True)
    parser.add_argument("--object", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--runner", required=True)
    parser.add_argument("--report", required=True)
    return parser.parse_args()


@functools.cache
def normalized_path(value):
    if not value:
        return None
    try:
        return str(Path(value).resolve())
    except OSError:
        return str(Path(value).absolute())


def location_parts(location):
    if not isinstance(location, dict):
        return []
    expansion = location.get("expansionLoc")
    spelling = location.get("spellingLoc")
    if isinstance(expansion, dict):
        return [("macro-expansion", expansion)]
    if isinstance(spelling, dict):
        return [("macro-spelling", spelling)]
    return [("location", location)]


def explicit_location(node):
    for label, location in location_parts(node.get("loc", {})):
        path = normalized_path(location.get("file"))
        if path:
            return path, label, location
    for label, location in location_parts(node.get("range", {}).get("begin", {})):
        path = normalized_path(location.get("file"))
        if path:
            return path, f"range-{label}", location
    return None


def elided_included_location(node):
    locations = [
        *location_parts(node.get("loc", {})),
        *[
            (f"range-{label}", location)
            for label, location in location_parts(
                node.get("range", {}).get("begin", {})
            )
        ],
    ]
    for label, location in locations:
        included_from = location.get("includedFrom", {})
        included_path = normalized_path(included_from.get("file"))
        if included_path and not location.get("file"):
            return label, location, included_path
    return None


class DeclarationIndex:
    def __init__(self, ast):
        self.nodes = {}
        self.parents = {}
        self.contexts = {}
        self.stream_origins = {}
        self._current_origin = None
        self._walk(ast, None)

    def _walk(self, node, parent):
        node_id = node.get("id")
        located = explicit_location(node)
        included = elided_included_location(node)
        location_kind = "ast-stream-inherited"
        if located is not None:
            self._current_origin = located[0]
            location_kind = located[1]
        elif included is not None:
            included_from = included[2]
            if (
                self._current_origin is None
                or self._current_origin == included_from
            ):
                self._current_origin = INCLUDED_FILE_ORIGIN
            location_kind = f"{included[0]}-included-path-elided"
        if node_id:
            self.nodes[node_id] = node
            if self._current_origin is not None:
                self.stream_origins[node_id] = {
                    "path": self._current_origin,
                    "location_kind": location_kind,
                    "included_from": included[2] if included is not None else None,
                }
            if parent is not None and parent.get("id"):
                self.parents[node_id] = parent["id"]
            if node.get("kind") in CONTEXT_KINDS:
                self.contexts[node_id] = node
        for child in node.get("inner", []):
            self._walk(child, node)

    def related_nodes(self, node):
        parent_id = self.parents.get(node.get("id"))
        if parent_id in self.nodes:
            yield "lexical-parent", self.nodes[parent_id]
        relation_fields = (
            "parentDeclContextId",
            "canonicalDecl",
            "previousDecl",
            "specializedTemplate",
            "instantiatedFrom",
            "instantiatedFromMember",
            "TemplateInstantiationPattern",
        )
        for field in relation_fields:
            related = node.get(field)
            related_id = related.get("id") if isinstance(related, dict) else related
            if related_id in self.nodes:
                yield field, self.nodes[related_id]

    def stream_origin(self, node):
        return self.stream_origins.get(node.get("id"))

    def context_chain(self, node):
        result = []
        seen = set()
        current = node
        while current is not None and current.get("id") not in seen:
            current_id = current.get("id")
            if current_id:
                seen.add(current_id)
            result.append(current)
            context_id = current.get("parentDeclContextId")
            if context_id not in self.nodes:
                context_id = self.parents.get(current_id)
            current = self.nodes.get(context_id)
        return result


def resolve_origin(node, index):
    pending = [(node, "declaration")]
    visited = set()
    chain = []
    while pending:
        current, relation = pending.pop(0)
        current_id = current.get("id")
        if current_id in visited:
            continue
        if current_id:
            visited.add(current_id)
        stream_origin = index.stream_origin(current)
        if stream_origin is not None:
            location = current.get("loc", {})
            if "expansionLoc" in location:
                location = location["expansionLoc"]
            entry = {
                "relation": relation,
                "path": stream_origin["path"],
                "location_kind": stream_origin["location_kind"],
                "line": location.get("line"),
                "column": location.get("col"),
            }
            if stream_origin["included_from"] is not None:
                entry["included_from"] = stream_origin["included_from"]
            chain.append(entry)
            return stream_origin["path"], chain
        located = explicit_location(current)
        if located is not None:
            path, location_kind, location = located
            chain.append(
                {
                    "relation": relation,
                    "path": path,
                    "location_kind": location_kind,
                    "line": location.get("line"),
                    "column": location.get("col"),
                }
            )
            return path, chain
        included = elided_included_location(current)
        if included is not None:
            location_kind, location, included_from = included
            chain.append(
                {
                    "relation": relation,
                    "path": INCLUDED_FILE_ORIGIN,
                    "included_from": included_from,
                    "location_kind": f"{location_kind}-included-path-elided",
                    "line": location.get("line"),
                    "column": location.get("col"),
                }
            )
            return INCLUDED_FILE_ORIGIN, chain
        for next_relation, related in index.related_nodes(current):
            pending.append((related, next_relation))
    return None, chain


def context_name(node):
    if node.get("kind") == "NamespaceDecl" and not node.get("name"):
        return "(anonymous namespace)"
    return node.get("name")


def qualified_name(node, index):
    names = []
    for context in reversed(index.context_chain(node)[1:]):
        name = context_name(context)
        if name and (not names or names[-1] != name):
            names.append(name)
    name = node.get("name")
    if name and (not names or names[-1] != name):
        names.append(name)
    return "::".join(names)


def has_function_body(node):
    return any(child.get("kind") in BODY_KINDS for child in node.get("inner", []))


def lexical_parent(node, index):
    return index.nodes.get(index.parents.get(node.get("id")))


def is_namespace_or_member_variable(node, index):
    parent = lexical_parent(node, index)
    if parent is None:
        return False
    while parent.get("kind") == "VarTemplateDecl":
        parent = lexical_parent(parent, index)
        if parent is None:
            return False
    if parent.get("kind") in {"TranslationUnitDecl", "NamespaceDecl"}:
        return True
    context_id = node.get("parentDeclContextId")
    context = index.nodes.get(context_id)
    return context is not None and context.get("kind") in {
        "ClassTemplateSpecializationDecl",
        "CXXRecordDecl",
        "RecordDecl",
    }


def is_definition(node, index):
    kind = node.get("kind")
    if kind in FUNCTION_KINDS:
        return has_function_body(node)
    if kind in VARIABLE_KINDS:
        if not is_namespace_or_member_variable(node, index):
            return False
        return node.get("storageClass") != "extern"
    if kind in INSTANTIATION_KINDS:
        return bool(
            node.get("specializationKind")
            or node.get("isExplicitInstantiation")
            or node.get("isThisDeclarationADefinition")
            or node.get("completeDefinition")
        )
    return False


def has_anonymous_namespace_ancestor(node, index):
    return any(
        context.get("kind") == "NamespaceDecl" and not context.get("name")
        for context in index.context_chain(node)[1:]
    )


def is_record_context(node):
    return node.get("kind") in {
        "ClassTemplateDecl",
        "ClassTemplateSpecializationDecl",
        "CXXRecordDecl",
        "RecordDecl",
    }


def linkage_classification(node, index):
    if has_anonymous_namespace_ancestor(node, index):
        return "internal", "anonymous-namespace ancestry"
    kind = node.get("kind")
    parent = lexical_parent(node, index)
    context = index.nodes.get(node.get("parentDeclContextId"))
    is_member = context is not None and is_record_context(context)
    namespace_scope = parent is not None and parent.get("kind") in {
        "NamespaceDecl",
        "TranslationUnitDecl",
    }
    if (
        kind in FUNCTION_KINDS | VARIABLE_KINDS
        and namespace_scope
        and not is_member
        and node.get("storageClass") == "static"
    ):
        return "internal", "namespace-scope static"
    if kind in VARIABLE_KINDS and namespace_scope and not is_member:
        spelling = node.get("type", {}).get("qualType", "")
        if (
            spelling.startswith("const ")
            and node.get("storageClass") != "extern"
            and not node.get("inline")
        ):
            return "internal", "namespace-scope non-inline const"
    return "external", "externally linked definition"


def is_approved_local_descendant(node, index, expected_runner):
    saw_record = False
    for context in index.context_chain(node)[1:]:
        if is_record_context(context):
            saw_record = True
            continue
        if not saw_record or context.get("kind") not in FUNCTION_KINDS:
            continue
        linkage, _ = linkage_classification(context, index)
        is_runner = (
            qualified_name(context, index) == expected_runner
            and context.get("type", {}).get("qualType")
            == "int (int, char **)"
        )
        return linkage == "internal" or is_runner
    return False


def iter_nodes(node):
    yield node
    for child in node.get("inner", []):
        yield from iter_nodes(child)


def report_location(node):
    location = node.get("loc", {})
    if "expansionLoc" in location:
        location = location["expansionLoc"]
    return {
        key: location.get(ast_key)
        for key, ast_key in (
            ("file", "file"),
            ("line", "line"),
            ("column", "col"),
        )
        if location.get(ast_key) is not None
    }


def audit(ast, source_path, expected_runner):
    source_path = normalized_path(source_path)
    index = DeclarationIndex(ast)
    declarations = []
    for node in iter_nodes(ast):
        if node.get("isImplicit") or not is_definition(node, index):
            continue
        if is_approved_local_descendant(node, index, expected_runner):
            continue
        linkage, reason = linkage_classification(node, index)
        name = qualified_name(node, index)
        origin, origin_chain = resolve_origin(node, index)
        if origin is None and linkage == "external":
            declarations.append(
                {
                    "kind": node.get("kind"),
                    "name": name,
                    "linkage": "unclassifiable",
                    "status": "unclassifiable",
                    "type": node.get("type", {}).get("qualType"),
                    "location": report_location(node),
                    "origin_chain": origin_chain,
                    "reason": "external definition has no classifiable origin",
                }
            )
            continue
        if origin != source_path:
            continue
        status = "internal"
        if linkage == "external":
            expected_type = node.get("type", {}).get("qualType") == (
                "int (int, char **)"
            )
            if name == expected_runner and expected_type:
                status = "approved"
            else:
                status = "unexpected"
        declarations.append(
            {
                "kind": node.get("kind"),
                "name": name,
                "linkage": linkage,
                "status": status,
                "type": node.get("type", {}).get("qualType"),
                "location": report_location(node),
                "origin_chain": origin_chain,
                "reason": reason,
            }
        )
    approved = [
        declaration
        for declaration in declarations
        if declaration["status"] == "approved"
    ]
    failures = [
        declaration
        for declaration in declarations
        if declaration["status"] in {"unexpected", "unclassifiable"}
    ]
    if len(approved) != 1:
        failures.append(
            {
                "kind": "RunnerRequirement",
                "name": expected_runner,
                "linkage": "external",
                "status": "unclassifiable",
                "type": "int (int, char **)",
                "location": {},
                "origin_chain": [],
                "reason": f"expected exactly one runner definition, found {len(approved)}",
            }
        )
    return {
        "source": source_path,
        "expected_runner": expected_runner,
        "declarations": declarations,
        "failures": failures,
    }


def write_report(path, report):
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=destination.name + ".", suffix=".tmp", dir=destination.parent
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            json.dump(report, stream, indent=2, sort_keys=True)
            stream.write("\n")
        os.replace(temporary, destination)
    except BaseException:
        Path(temporary).unlink(missing_ok=True)
        raise


def main():
    args = parse_args()
    verify_manifest_clang(args.clang)
    report = audit(
        load_ast(
            args.compile_commands, args.source, args.clang, args.object
        ),
        args.source,
        args.runner,
    )
    write_report(args.report, report)
    if not report["failures"]:
        return 0
    print(
        f"app linkage audit found {len(report['failures'])} failure(s); "
        f"report: {Path(args.report).resolve()}",
        file=sys.stderr,
    )
    for failure in report["failures"][:10]:
        print(
            f"  {failure['kind']} {failure['name']}: {failure['reason']}",
            file=sys.stderr,
        )
    return 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"app linkage audit failed: {error}", file=sys.stderr)
        sys.exit(1)
