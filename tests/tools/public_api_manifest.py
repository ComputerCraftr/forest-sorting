#!/usr/bin/env python3

import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from clang_ast import load_ast, verify_manifest_clang
from public_api_manifest_normalize import (
    IGNORED_DIRECT_KINDS,
    IGNORED_MEMBER_KINDS,
    PUBLIC_MEMBER_KINDS,
    PUBLIC_TOP_LEVEL_KINDS,
    SourceRepository,
    canonical_attributes,
    canonical_constraint,
    canonical_parameter_defaults,
    canonical_template,
    canonical_type_from_spelling,
    declaration_flags,
    first_evaluated_value,
    normalize_space,
    normalized_type,
    record_capabilities,
    record_definition,
    source_location,
    template_parameter_map,
)


def declaration_entry(
    kind,
    qualified_name,
    node,
    sources,
    source_path,
    access=None,
    inherited_parameters=None,
):
    result = {"kind": kind, "name": qualified_name}
    template_parameters = dict(inherited_parameters or {})
    if kind in {
        "ClassTemplateDecl",
        "ConceptDecl",
        "FunctionTemplateDecl",
        "TypeAliasTemplateDecl",
        "VarTemplateDecl",
    }:
        template, template_parameters = canonical_template(
            node, sources, source_path, inherited_parameters
        )
        result["template"] = template
        templated = next(
            (
                child
                for child in node.get("inner", [])
                if child.get("kind")
                in {
                    "CXXMethodDecl",
                    "CXXRecordDecl",
                    "FunctionDecl",
                    "TypeAliasDecl",
                    "VarDecl",
                }
            ),
            None,
        )
        if templated is not None and templated.get("type", {}).get("qualType"):
            result["type"] = normalized_type(
                templated, template_parameters
            )
            flags = declaration_flags(templated)
            if flags:
                result["flags"] = flags
            defaults = canonical_parameter_defaults(
                templated,
                template_parameters,
                sources,
                source_path,
            )
            if defaults is not None:
                result["parameter_defaults"] = defaults
    elif node.get("type", {}).get("qualType"):
        result["type"] = normalized_type(node, template_parameters)
        flags = declaration_flags(node)
        if flags:
            result["flags"] = flags
        if kind in {
            "CXXConstructorDecl",
            "CXXConversionDecl",
            "CXXMethodDecl",
            "FunctionDecl",
        }:
            defaults = canonical_parameter_defaults(
                node, template_parameters, sources, source_path
            )
            if defaults is not None:
                result["parameter_defaults"] = defaults
    constraint = canonical_constraint(
        node, template_parameters, sources, source_path
    )
    if constraint is not None:
        result["constraint"] = constraint
    attributes = canonical_attributes(node, sources, source_path)
    if attributes:
        result["attributes"] = attributes
    record = record_definition(node)
    if record is not None:
        result["record"] = {
            "tag": record.get("tagUsed", "class"),
            "bases": [
                {
                    "access": base.get(
                        "access", base.get("writtenAccess", "")
                    ),
                    "type": canonical_type_from_spelling(
                        base.get("type", {}).get("qualType", ""),
                        template_parameters,
                    ),
                    "virtual": bool(base.get("isVirtual", False)),
                }
                for base in record.get("bases", [])
            ],
            "capabilities": record_capabilities(record),
        }
    if kind == "EnumDecl":
        result["underlying"] = canonical_type_from_spelling(
            node.get("fixedUnderlyingType", {}).get("qualType", "")
        )
        result["scoped"] = bool(node.get("scopedEnumTag"))
    if kind == "EnumConstantDecl":
        value = first_evaluated_value(node)
        if value is not None:
            result["value"] = value
    if kind == "VarDecl" and (
        node.get("constexpr")
        or normalize_space(node.get("type", {}).get("qualType", "")).startswith(
            "const "
        )
    ):
        value = first_evaluated_value(node)
        if value is not None:
            result["value"] = value
    if kind == "FieldDecl" and node.get("isBitfield"):
        width = first_evaluated_value(node)
        if width is not None:
            result["bit_width"] = width
    if access is not None:
        result["access"] = access
    return result


def canonical_id(node, nodes_by_id):
    current = node
    visited = set()
    while current.get("previousDecl") and current["previousDecl"] not in visited:
        visited.add(current["previousDecl"])
        previous = nodes_by_id.get(current["previousDecl"])
        if previous is None:
            break
        current = previous
    return current.get("id", node.get("id"))


def index_nodes(node, result):
    node_id = node.get("id")
    if node_id:
        result[node_id] = node
    for child in node.get("inner", []):
        index_nodes(child, result)


def node_file(node, inherited_file, sources):
    location = source_location(node.get("loc", {}))
    return sources.resolve(location.get("file")) or inherited_file


def record_members(
    record,
    qualified_name,
    entries,
    seen,
    nodes_by_id,
    sources,
    source_path,
    template_parameters,
):
    default_access = (
        "public" if record.get("tagUsed") in {"struct", "union"} else "private"
    )
    current_access = default_access
    for child in record.get("inner", []):
        kind = child.get("kind")
        if kind == "AccessSpecDecl":
            current_access = child.get("access", current_access)
            continue
        if child.get("isImplicit"):
            continue
        access = child.get("access", current_access)
        if access == "private" or kind in IGNORED_MEMBER_KINDS:
            continue
        if kind == "FriendDecl":
            friends = [
                item
                for item in child.get("inner", [])
                if item.get("kind") in {"FunctionDecl", "FunctionTemplateDecl"}
            ]
            if len(friends) != 1:
                raise RuntimeError(
                    f"unclassified friend declaration in {qualified_name}"
                )
            friend = friends[0]
            canonical = canonical_id(friend, nodes_by_id)
            if canonical in seen:
                continue
            seen.add(canonical)
            if not friend.get("name"):
                raise RuntimeError(
                    f"unnamed friend declaration in {qualified_name}"
                )
            entries.append(
                declaration_entry(
                    friend["kind"],
                    f"forest_sorting::{friend['name']}",
                    friend,
                    sources,
                    source_path,
                    inherited_parameters=template_parameters,
                )
            )
            continue
        if kind not in PUBLIC_MEMBER_KINDS:
            raise RuntimeError(
                f"unclassified public member kind {kind} in {qualified_name}"
            )
        canonical = canonical_id(child, nodes_by_id)
        if canonical in seen:
            continue
        seen.add(canonical)
        child_name = child.get("name")
        if not child_name:
            raise RuntimeError(
                f"unnamed public member kind {kind} in {qualified_name}"
            )
        child_qualified = f"{qualified_name}::{child_name}"
        entries.append(
            declaration_entry(
                kind,
                child_qualified,
                child,
                sources,
                source_path,
                access,
                template_parameters,
            )
        )
        child_record = record_definition(child)
        if child_record is not None:
            child_template_parameters = dict(template_parameters)
            if kind == "ClassTemplateDecl":
                _, child_template_parameters = canonical_template(
                    child,
                    sources,
                    source_path,
                    template_parameters,
                )
            record_members(
                child_record,
                child_qualified,
                entries,
                seen,
                nodes_by_id,
                sources,
                source_path,
                child_template_parameters,
            )
        if kind == "EnumDecl":
            append_enumerators(
                child,
                child_qualified,
                entries,
                sources,
                source_path,
            )


def append_enumerators(enum, qualified_name, entries, sources, source_path):
    for enumerator in enum.get("inner", []):
        if enumerator.get("kind") == "EnumConstantDecl":
            entries.append(
                declaration_entry(
                    "EnumConstantDecl",
                    f"{qualified_name}::{enumerator['name']}",
                    enumerator,
                    sources,
                    source_path,
                )
            )


def manifest_from_ast(root, sources, configuration, main_source):
    nodes_by_id = {}
    index_nodes(root, nodes_by_id)
    entries = []
    seen = set()
    for node in root.get("inner", []):
        if (
            node.get("kind") != "NamespaceDecl"
            or node.get("name") != "forest_sorting"
        ):
            continue
        namespace_file = node_file(node, main_source, sources)
        for child in node.get("inner", []):
            kind = child.get("kind")
            if kind == "NamespaceDecl" and child.get("name") == "detail":
                continue
            if child.get("isImplicit") or kind in IGNORED_DIRECT_KINDS:
                continue
            if kind == "NamespaceDecl":
                raise RuntimeError(
                    "unclassified public nested namespace "
                    f"forest_sorting::{child.get('name', '<anonymous>')}"
                )
            if kind not in PUBLIC_TOP_LEVEL_KINDS:
                raise RuntimeError(
                    f"unclassified public declaration kind {kind}"
                )
            canonical = canonical_id(child, nodes_by_id)
            if canonical in seen:
                continue
            seen.add(canonical)
            name = child.get("name")
            if not name:
                raise RuntimeError(f"unnamed public declaration kind {kind}")
            source_path = node_file(child, namespace_file, sources)
            qualified_name = f"forest_sorting::{name}"
            entries.append(
                declaration_entry(
                    kind,
                    qualified_name,
                    child,
                    sources,
                    source_path,
                )
            )
            record = record_definition(child)
            if record is not None:
                template_parameters = (
                    template_parameter_map(child)
                    if kind == "ClassTemplateDecl"
                    else {}
                )
                record_members(
                    record,
                    qualified_name,
                    entries,
                    seen,
                    nodes_by_id,
                    sources,
                    source_path,
                    template_parameters,
                )
            if kind == "EnumDecl":
                append_enumerators(
                    child, qualified_name, entries, sources, source_path
                )
    return {
        "manifest_schema": 2,
        "configuration": configuration,
        "declarations": sorted(
            entries,
            key=lambda item: (
                item["name"],
                item["kind"],
                json.dumps(item, sort_keys=True),
            ),
        ),
    }


def declaration_groups(manifest):
    result = {}
    for declaration in manifest["declarations"]:
        key = (declaration["kind"], declaration["name"])
        result.setdefault(key, []).append(declaration)
    return result


def template_shape(declaration):
    parameters = declaration.get("template", {}).get("parameters", [])
    result = []
    for parameter in parameters:
        item = {
            "kind": parameter["kind"],
            "pack": parameter.get("pack", False),
        }
        if "type" in parameter:
            item["type"] = parameter["type"]
        result.append(item)
    return result


def overload_identity(declaration):
    identity = {
        "kind": declaration["kind"],
        "name": declaration["name"],
        "template": template_shape(declaration),
    }
    declaration_type = declaration.get("type", {})
    if declaration_type.get("kind") == "function":
        identity.update(
            {
                "parameters": declaration_type["parameters"],
                "const": declaration_type["const"],
                "volatile": declaration_type["volatile"],
                "ref_qualifier": declaration_type["ref_qualifier"],
            }
        )
    return identity


def compare_declaration_group(expected, actual):
    unmatched_actual = list(actual)
    removed = []
    changed = []
    for expected_declaration in expected:
        identity = overload_identity(expected_declaration)
        match_index = next(
            (
                index
                for index, actual_declaration in enumerate(unmatched_actual)
                if overload_identity(actual_declaration) == identity
            ),
            None,
        )
        if match_index is None:
            removed.append(expected_declaration)
            continue
        actual_declaration = unmatched_actual.pop(match_index)
        if expected_declaration != actual_declaration:
            changed.append((expected_declaration, actual_declaration))
    return removed, unmatched_actual, changed


def check_manifest_schema(manifest):
    if manifest.get("manifest_schema") != 2:
        raise RuntimeError("public API manifest must use schema 2")

    def visit(value):
        if isinstance(value, dict):
            for key, child in value.items():
                lowered = key.lower()
                if "digest" in lowered or "hash" in lowered:
                    raise RuntimeError(
                        f"public API manifest contains forbidden key {key}"
                    )
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)

    visit(manifest)


def check_manifest(actual, manifest_path):
    expected = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    check_manifest_schema(expected)
    check_manifest_schema(actual)
    if actual == expected:
        return
    expected_groups = declaration_groups(expected)
    actual_groups = declaration_groups(actual)
    added = sorted(set(actual_groups) - set(expected_groups))
    removed = sorted(set(expected_groups) - set(actual_groups))
    lines = ["public API manifest changed"]
    if added:
        lines.append(
            "Added:\n"
            + "\n".join(f"  {kind} {name}" for kind, name in added)
        )
    if removed:
        lines.append(
            "Removed:\n"
            + "\n".join(f"  {kind} {name}" for kind, name in removed)
        )
    for key in sorted(set(expected_groups) & set(actual_groups)):
        removed_overloads, added_overloads, changed_overloads = (
            compare_declaration_group(
                expected_groups[key], actual_groups[key]
            )
        )
        if not (
            removed_overloads or added_overloads or changed_overloads
        ):
            continue
        kind, name = key
        if removed_overloads:
            lines.append(f"Removed overloads:\n  {kind} {name}")
            lines.append(
                json.dumps(removed_overloads, indent=2, sort_keys=True)
            )
        if added_overloads:
            lines.append(f"Added overloads:\n  {kind} {name}")
            lines.append(
                json.dumps(added_overloads, indent=2, sort_keys=True)
            )
        for expected_declaration, actual_declaration in changed_overloads:
            lines.append(f"Changed:\n  {kind} {name}")
            lines.append(
                "Expected:\n"
                + json.dumps(
                    expected_declaration, indent=2, sort_keys=True
                )
            )
            lines.append(
                "Actual:\n"
                + json.dumps(actual_declaration, indent=2, sort_keys=True)
            )
    raise RuntimeError("\n\n".join(lines))


def header_guard_macro(lines):
    for line in lines[:8]:
        match = re.match(r"\s*#ifndef\s+([A-Za-z_][A-Za-z0-9_]*)", line)
        if match:
            return match.group(1)
    return None


def check_condition_registry(include_root, public_headers, registry):
    include_root_path = Path(include_root).resolve()
    canonical_headers = {
        str(Path(header).resolve().relative_to(include_root_path))
        for header in public_headers
    }
    configured_headers = {
        header
        for configuration in registry["configurations"].values()
        for header in configuration.get("headers", [])
    }
    if canonical_headers != configured_headers:
        raise RuntimeError(
            "public API configuration headers differ from the target FILE_SET: "
            f"target={sorted(canonical_headers)}, "
            f"configured={sorted(configured_headers)}"
        )
    observed = {}
    for header in public_headers:
        relative_header = str(
            Path(header).resolve().relative_to(include_root_path)
        )
        lines = Path(header).read_text(encoding="utf-8").splitlines()
        guard = header_guard_macro(lines)
        for line in lines:
            match = re.match(
                r"\s*#\s*(if|ifdef|ifndef|elif)\s+(.+)", line
            )
            if not match:
                continue
            directive = match.group(1)
            expression = normalize_space(match.group(2))
            if directive == "ifndef" and expression == guard:
                continue
            condition = f"{directive} {expression}"
            observed.setdefault(condition, set()).add(relative_header)
    registered = set(registry["conditions"])
    if set(observed) != registered:
        raise RuntimeError(
            "public API condition registry mismatch: "
            f"observed={sorted(observed)}, registered={sorted(registered)}"
        )
    for condition, headers in observed.items():
        configured = set(registry["conditions"][condition]["headers"])
        if headers != configured:
            raise RuntimeError(
                f"conditional {condition} header mismatch: "
                f"observed={sorted(headers)}, configured={sorted(configured)}"
            )
    configurations = registry["configurations"]
    for name, condition in registry["conditions"].items():
        present = condition["present_configuration"]
        absent = condition["absent_configuration"]
        if present not in configurations or absent not in configurations:
            raise RuntimeError(
                f"conditional {name} references an unknown configuration"
            )
        if present == absent:
            raise RuntimeError(
                f"conditional {name} uses one configuration for both branches"
            )
        if "manifest" not in configurations[present]:
            raise RuntimeError(
                f"conditional {name} present configuration has no manifest"
            )
        absent_configuration = configurations[absent]
        if not (
            "manifest" in absent_configuration
            or "compile_fail_headers" in absent_configuration
        ):
            raise RuntimeError(
                f"conditional {name} absent configuration has no proof"
            )


def check_probe_headers(source_path, configuration_name, registry):
    expected = registry["configurations"][configuration_name].get(
        "headers", []
    )
    source = Path(source_path).read_text(encoding="utf-8")
    actual = re.findall(
        r"^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]",
        source,
        flags=re.MULTILINE,
    )
    if actual != expected:
        raise RuntimeError(
            f"{configuration_name} probe headers differ from its registry: "
            f"actual={actual}, expected={expected}"
        )


def check_uint128_unavailable(clang, include_root, registry):
    configuration = registry["configurations"]["uint128-unavailable"]
    for header, diagnostic in configuration["compile_fail_headers"].items():
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "probe.cpp"
            source.write_text(f"#include <{header}>\n", encoding="utf-8")
            completed = subprocess.run(
                [
                    clang,
                    "-std=c++20",
                    f"-I{include_root}",
                    "-U__SIZEOF_INT128__",
                    "-fsyntax-only",
                    str(source),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            if completed.returncode == 0 or diagnostic not in completed.stderr:
                raise RuntimeError(
                    f"{header} did not fail with its UInt128 diagnostic:\n"
                    + completed.stderr
                )


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--clang", required=True)
    parser.add_argument("--compile-commands")
    parser.add_argument("--source")
    parser.add_argument("--manifest")
    parser.add_argument("--configuration")
    parser.add_argument("--include-root")
    parser.add_argument("--registry")
    parser.add_argument("--public-header", action="append", default=[])
    parser.add_argument("--write-manifest", action="store_true")
    return parser.parse_args()


def main():
    args = parse_args()
    required = (
        args.compile_commands,
        args.source,
        args.manifest,
        args.configuration,
        args.include_root,
        args.registry,
    )
    if any(value is None for value in required):
        raise RuntimeError(
            "manifest mode requires compile, source, registry, and manifest arguments"
        )
    verify_manifest_clang(args.clang)
    registry = json.loads(Path(args.registry).read_text(encoding="utf-8"))
    check_condition_registry(args.include_root, args.public_header, registry)
    check_probe_headers(args.source, args.configuration, registry)
    if args.configuration == "uint128-enabled":
        check_uint128_unavailable(args.clang, args.include_root, registry)
    sources = SourceRepository([*args.public_header, args.source])
    actual = manifest_from_ast(
        load_ast(args.compile_commands, args.source, args.clang),
        sources,
        args.configuration,
        str(Path(args.source).resolve()),
    )
    check_manifest_schema(actual)
    if args.write_manifest:
        Path(args.manifest).write_text(
            json.dumps(actual, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    else:
        check_manifest(actual, args.manifest)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"public API manifest check failed: {error}", file=sys.stderr)
        sys.exit(1)
