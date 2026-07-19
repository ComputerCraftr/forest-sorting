#!/usr/bin/env python3

import re
from pathlib import Path


PUBLIC_TOP_LEVEL_KINDS = {
    "ClassTemplateDecl",
    "ConceptDecl",
    "CXXRecordDecl",
    "EnumDecl",
    "FunctionDecl",
    "FunctionTemplateDecl",
    "NamespaceAliasDecl",
    "TypeAliasDecl",
    "TypeAliasTemplateDecl",
    "TypedefDecl",
    "UsingDecl",
    "UsingDirectiveDecl",
    "VarDecl",
    "VarTemplateDecl",
}

PUBLIC_MEMBER_KINDS = {
    "ClassTemplateDecl",
    "CXXConstructorDecl",
    "CXXConversionDecl",
    "CXXDestructorDecl",
    "CXXMethodDecl",
    "CXXRecordDecl",
    "EnumDecl",
    "FieldDecl",
    "FunctionTemplateDecl",
    "TypeAliasDecl",
    "TypeAliasTemplateDecl",
    "TypedefDecl",
    "UsingDecl",
    "UsingDirectiveDecl",
    "VarDecl",
    "VarTemplateDecl",
}

TEMPLATE_PARAMETER_KINDS = {
    "NonTypeTemplateParmDecl",
    "TemplateTemplateParmDecl",
    "TemplateTypeParmDecl",
}

CONSTRAINT_KINDS = {
    "AtomicConstraint",
    "BinaryOperator",
    "ConceptSpecializationExpr",
    "ConstraintExpr",
    "FoldExpr",
    "ParenExpr",
    "RequiresExpr",
    "UnaryOperator",
}

IGNORED_DIRECT_KINDS = {"EmptyDecl", "StaticAssertDecl"}
IGNORED_MEMBER_KINDS = {"AccessSpecDecl", "StaticAssertDecl"}
EXPRESSION_WRAPPERS = {
    "AtomicConstraint",
    "ConstantExpr",
    "ExprWithCleanups",
    "FullExpr",
    "ImplicitCastExpr",
    "MaterializeTemporaryExpr",
    "ParenExpr",
}

SOURCE_EXPRESSION_KINDS = {
    "ArraySubscriptExpr",
    "CallExpr",
    "CXXBoolLiteralExpr",
    "CXXDependentScopeMemberExpr",
    "CXXFunctionalCastExpr",
    "CXXMemberCallExpr",
    "CXXNoexceptExpr",
    "CXXNullPtrLiteralExpr",
    "CXXStaticCastExpr",
    "CharacterLiteral",
    "ConditionalOperator",
    "DeclRefExpr",
    "DependentScopeDeclRefExpr",
    "FloatingLiteral",
    "InitListExpr",
    "IntegerLiteral",
    "MemberExpr",
    "PackExpansionExpr",
    "SizeOfPackExpr",
    "StringLiteral",
    "SubstNonTypeTemplateParmExpr",
    "UnaryExprOrTypeTraitExpr",
    "UnresolvedLookupExpr",
}

TYPE_TOKEN_PATTERN = re.compile(
    r"::|&&|\.\.\.|->|[A-Za-z_][A-Za-z0-9_]*|"
    r"0[xX][0-9A-Fa-f]+|[0-9]+|[<>,*&()[\]]"
)
SOURCE_TOKEN_PATTERN = re.compile(
    r"::|&&|\|\||==|!=|<=|>=|->|\.\.\.|<<|>>|"
    r"[A-Za-z_][A-Za-z0-9_]*|0[xX][0-9A-Fa-f]+|"
    r"[0-9]+|[<>{}(),;:*&!+\-/=\[\].]"
)
STD_INLINE_NAMESPACE = re.compile(
    r"\bstd::__(?:1|cxx11|ndk1|[0-9]+)::"
)


def normalize_space(value):
    return " ".join(value.split())


def normalize_standard_namespaces(value):
    previous = None
    while previous != value:
        previous = value
        value = STD_INLINE_NAMESPACE.sub("std::", value)
    return value


def normalized_qualified_name(tokens):
    value = "".join(tokens)
    return normalize_standard_namespaces(value)


def split_top_level(tokens, delimiter):
    result = []
    current = []
    angle = paren = bracket = 0
    for token in tokens:
        if token == "<":
            angle += 1
        elif token == ">":
            angle -= 1
        elif token == "(":
            paren += 1
        elif token == ")":
            paren -= 1
        elif token == "[":
            bracket += 1
        elif token == "]":
            bracket -= 1
        if token == delimiter and angle == paren == bracket == 0:
            result.append(current)
            current = []
        else:
            current.append(token)
    result.append(current)
    return result


def matching_token(tokens, begin, opening, closing):
    depth = 0
    for index in range(begin, len(tokens)):
        if tokens[index] == opening:
            depth += 1
        elif tokens[index] == closing:
            depth -= 1
            if depth == 0:
                return index
    raise RuntimeError(f"unbalanced type tokens: {tokens}")


def top_level_function_paren(tokens):
    angle = bracket = 0
    for index, token in enumerate(tokens):
        if token == "<":
            angle += 1
        elif token == ">":
            angle -= 1
        elif token == "[":
            bracket += 1
        elif token == "]":
            bracket -= 1
        elif token == "(" and angle == bracket == 0:
            end = matching_token(tokens, index, "(", ")")
            suffix = tokens[end + 1 :]
            if all(
                item in {"const", "volatile", "&", "&&", "noexcept"}
                for item in suffix
            ):
                return index, end
    return None


def canonical_type_from_spelling(spelling, template_parameters=None):
    spelling = normalize_standard_namespaces(normalize_space(spelling))
    tokens = TYPE_TOKEN_PATTERN.findall(spelling)
    if not tokens:
        return {"kind": "empty"}
    template_parameters = template_parameters or {}
    function = top_level_function_paren(tokens)
    if function is not None:
        begin, end = function
        parameters = []
        parameter_tokens = tokens[begin + 1 : end]
        if parameter_tokens and parameter_tokens != ["void"]:
            parameters = [
                canonical_type_tokens(part, template_parameters)
                for part in split_top_level(parameter_tokens, ",")
            ]
        suffix = tokens[end + 1 :]
        return {
            "kind": "function",
            "return": canonical_type_tokens(
                tokens[:begin], template_parameters
            ),
            "parameters": parameters,
            "const": "const" in suffix,
            "volatile": "volatile" in suffix,
            "ref_qualifier": (
                "&&" if "&&" in suffix else "&" if "&" in suffix else "none"
            ),
            "noexcept": "noexcept" in suffix,
        }
    return canonical_type_tokens(tokens, template_parameters)


def canonical_type_tokens(tokens, template_parameters):
    if not tokens:
        raise RuntimeError("empty type token sequence")
    tokens = list(tokens)
    if tokens[0] == "typename":
        return {
            "kind": "dependent-name",
            "name": normalized_qualified_name(tokens[1:]),
        }
    if tokens[-1] in {"&", "&&"}:
        return {
            "kind": (
                "lvalue-reference" if tokens[-1] == "&" else "rvalue-reference"
            ),
            "to": canonical_type_tokens(tokens[:-1], template_parameters),
        }
    if tokens[-1] == "*":
        return {
            "kind": "pointer",
            "to": canonical_type_tokens(tokens[:-1], template_parameters),
        }
    if tokens[-1] == "]":
        begin = len(tokens) - 1
        while begin >= 0 and tokens[begin] != "[":
            begin -= 1
        if begin < 0:
            raise RuntimeError(f"unbalanced array type: {tokens}")
        extent = "".join(tokens[begin + 1 : -1])
        return {
            "kind": "array",
            "extent": extent or None,
            "element": canonical_type_tokens(
                tokens[:begin], template_parameters
            ),
        }
    qualifiers = []
    while tokens and tokens[0] in {"const", "volatile"}:
        qualifiers.append(tokens.pop(0))
    while tokens and tokens[-1] in {"const", "volatile"}:
        qualifiers.append(tokens.pop())
    if qualifiers:
        return {
            "kind": "qualified",
            "qualifiers": sorted(set(qualifiers)),
            "type": canonical_type_tokens(tokens, template_parameters),
        }
    angle_begin = next(
        (index for index, token in enumerate(tokens) if token == "<"), None
    )
    if angle_begin is not None:
        angle_end = matching_token(tokens, angle_begin, "<", ">")
        if angle_end != len(tokens) - 1:
            raise RuntimeError(f"unsupported trailing template type: {tokens}")
        arguments = split_top_level(tokens[angle_begin + 1 : angle_end], ",")
        return {
            "kind": "template-specialization",
            "name": normalized_qualified_name(tokens[:angle_begin]),
            "arguments": [
                canonical_type_tokens(argument, template_parameters)
                for argument in arguments
                if argument
            ],
        }
    name = normalized_qualified_name(tokens)
    if name in template_parameters:
        return {
            "kind": "template-parameter",
            "index": template_parameters[name],
        }
    builtins = {
        "bool",
        "char",
        "double",
        "float",
        "int",
        "long",
        "longdouble",
        "longlong",
        "short",
        "signedchar",
        "unsigned",
        "unsigned__int128",
        "unsignedchar",
        "unsignedint",
        "unsignedlong",
        "unsignedlonglong",
        "unsignedshort",
        "void",
    }
    compact_name = name.replace(" ", "")
    if compact_name in builtins:
        return {"kind": "builtin", "name": normalize_space(" ".join(tokens))}
    if name == "auto":
        return {"kind": "auto"}
    return {"kind": "named", "name": name}


def normalized_type(node, template_parameters=None):
    node_type = node.get("type", {})
    spelling = node_type.get("qualType", node_type.get("desugaredQualType", ""))
    return canonical_type_from_spelling(spelling, template_parameters)


def source_location(location):
    if "expansionLoc" in location:
        return source_location(location["expansionLoc"])
    if "spellingLoc" in location:
        return source_location(location["spellingLoc"])
    return location


class SourceRepository:
    def __init__(self, paths=()):
        self._sources = {}
        for path in paths:
            resolved = Path(path).resolve()
            self._sources[str(resolved)] = resolved.read_text(encoding="utf-8")

    def add_text(self, path, text):
        self._sources[str(Path(path).resolve())] = text

    def resolve(self, path):
        if path is None:
            return None
        candidate = Path(path)
        if candidate.is_absolute() and str(candidate) in self._sources:
            return str(candidate)
        suffix = str(candidate)
        matches = [
            item for item in self._sources if item.endswith("/" + suffix)
        ]
        if len(matches) == 1:
            return matches[0]
        return None

    def tokens(self, node, fallback_path):
        node_range = node.get("range", {})
        begin = source_location(node_range.get("begin", {}))
        end = source_location(node_range.get("end", {}))
        path = self.resolve(begin.get("file")) or self.resolve(fallback_path)
        if path is None or "offset" not in begin or "offset" not in end:
            return []
        text = self._sources[path]
        finish = end["offset"] + end.get("tokLen", 0)
        snippet = text[begin["offset"] : finish]
        return SOURCE_TOKEN_PATTERN.findall(snippet)


def explicit_template_arguments(node):
    return [
        child
        for child in node.get("inner", [])
        if child.get("kind") == "TemplateArgument" and child.get("range")
    ]


def canonical_template_argument(
    node, template_parameters, sources, source_path
):
    if node.get("type", {}).get("qualType"):
        return normalized_type(node, template_parameters)
    children = node.get("inner", [])
    if len(children) == 1:
        return canonical_expression(
            children[0], template_parameters, sources, source_path, {}
        )
    value = node.get("value")
    if value is not None:
        return {"kind": "value", "value": value}
    raise RuntimeError("unsupported explicit template argument")


def concept_name(node, sources, source_path):
    tokens = sources.tokens(node, source_path)
    if "<" in tokens:
        name = normalized_qualified_name(tokens[: tokens.index("<")])
        if "::" not in name:
            return f"forest_sorting::{name}"
        return name
    implicit = next(
        (
            child
            for child in node.get("inner", [])
            if child.get("kind") == "ImplicitConceptSpecializationDecl"
        ),
        None,
    )
    if implicit and implicit.get("name"):
        return normalize_standard_namespaces(implicit["name"])
    if tokens and all(
        token == "::" or re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", token)
        for token in tokens
    ):
        name = normalized_qualified_name(tokens)
        if "::" not in name:
            return f"forest_sorting::{name}"
        return name
    raise RuntimeError(
        "concept specialization has no source-level name: "
        f"source={source_path}, range={node.get('range')}, tokens={tokens}"
    )


def expression_tokens(
    node, sources, source_path, parameters, template_parameters
):
    tokens = sources.tokens(node, source_path)
    normalized = []
    index = 0
    while index < len(tokens):
        if (
            index + 3 < len(tokens)
            and tokens[index] == "std"
            and tokens[index + 1] == "::"
            and re.fullmatch(r"__(?:1|cxx11|ndk1|[0-9]+)", tokens[index + 2])
            and tokens[index + 3] == "::"
        ):
            normalized.extend(("std", "::"))
            index += 4
            continue
        token = tokens[index]
        if token in parameters:
            normalized.append(f"$requirement:{parameters[token]}")
        elif token in template_parameters:
            normalized.append(f"$template:{template_parameters[token]}")
        else:
            normalized.append(token)
        index += 1
    return normalized


def canonical_expression(
    node, template_parameters, sources, source_path, parameters
):
    kind = node.get("kind")
    if kind in EXPRESSION_WRAPPERS:
        children = node.get("inner", [])
        if len(children) != 1:
            raise RuntimeError(f"{kind} does not contain exactly one expression")
        return canonical_expression(
            children[0],
            template_parameters,
            sources,
            source_path,
            parameters,
        )
    if kind == "ConceptSpecializationExpr":
        return {
            "kind": "concept-specialization",
            "name": concept_name(node, sources, source_path),
            "arguments": [
                canonical_template_argument(
                    argument, template_parameters, sources, source_path
                )
                for argument in explicit_template_arguments(node)
            ],
        }
    if kind == "BinaryOperator":
        children = node.get("inner", [])
        if len(children) != 2:
            raise RuntimeError("binary constraint does not have two operands")
        operator = node.get("opcode", "")
        operands = [
            canonical_expression(
                child,
                template_parameters,
                sources,
                source_path,
                parameters,
            )
            for child in children
        ]
        if operator in {"&&", "||"}:
            semantic_kind = (
                "logical-and" if operator == "&&" else "logical-or"
            )
            flattened = []
            for operand in operands:
                if operand.get("kind") == semantic_kind:
                    flattened.extend(operand["operands"])
                else:
                    flattened.append(operand)
            return {"kind": semantic_kind, "operands": flattened}
        return {
            "kind": "binary-expression",
            "operator": operator,
            "operands": operands,
        }
    if kind == "UnaryOperator":
        children = node.get("inner", [])
        if len(children) != 1:
            raise RuntimeError("unary constraint does not have one operand")
        return {
            "kind": "unary-expression",
            "operator": node.get("opcode", ""),
            "operand": canonical_expression(
                children[0],
                template_parameters,
                sources,
                source_path,
                parameters,
            ),
        }
    if kind == "RequiresExpr":
        local_parameters = dict(parameters)
        requirement_parameters = []
        requirements = []
        for child in node.get("inner", []):
            child_kind = child.get("kind")
            if child_kind == "ParmVarDecl":
                parameter_index = len(requirement_parameters)
                local_parameters[child.get("name", "")] = parameter_index
                requirement_parameters.append(
                    normalized_type(child, template_parameters)
                )
            elif child_kind.endswith("Requirement"):
                requirements.append(
                    canonical_requirement(
                        child,
                        template_parameters,
                        sources,
                        source_path,
                        local_parameters,
                    )
                )
            else:
                raise RuntimeError(
                    f"unsupported requires-expression child {child_kind}"
                )
        return {
            "kind": "requires",
            "parameters": requirement_parameters,
            "requirements": requirements,
        }
    if kind not in SOURCE_EXPRESSION_KINDS:
        raise RuntimeError(f"unsupported semantic expression {kind}")
    tokens = expression_tokens(
        node, sources, source_path, parameters, template_parameters
    )
    if tokens:
        return {"kind": "expression", "tokens": tokens}
    value = node.get("value")
    if value is not None:
        return {"kind": "literal", "value": value}
    raise RuntimeError(f"semantic expression {kind} has no source representation")


def canonical_requirement(
    node, template_parameters, sources, source_path, parameters
):
    kind = node["kind"]
    children = node.get("inner", [])
    if kind == "TypeRequirement":
        if len(children) != 1:
            raise RuntimeError("type requirement does not contain one type")
        return {
            "kind": "type-requirement",
            "type": normalized_type(children[0], template_parameters),
        }
    if kind == "SimpleRequirement":
        if len(children) != 1:
            raise RuntimeError("simple requirement does not contain one expression")
        return {
            "kind": "simple-requirement",
            "expression": canonical_expression(
                children[0],
                template_parameters,
                sources,
                source_path,
                parameters,
            ),
        }
    if kind == "CompoundRequirement":
        expressions = [
            child
            for child in children
            if child.get("kind") != "ConceptSpecializationExpr"
        ]
        constraints = [
            child
            for child in children
            if child.get("kind") == "ConceptSpecializationExpr"
        ]
        if len(expressions) != 1 or len(constraints) > 1:
            raise RuntimeError("unsupported compound requirement shape")
        source_tokens = sources.tokens(node, source_path)
        result = {
            "kind": "compound-requirement",
            "expression": canonical_expression(
                expressions[0],
                template_parameters,
                sources,
                source_path,
                parameters,
            ),
            "noexcept": "noexcept" in source_tokens,
        }
        if constraints:
            result["return_constraint"] = canonical_expression(
                constraints[0],
                template_parameters,
                sources,
                source_path,
                parameters,
            )
        return result
    if kind == "NestedRequirement":
        if len(children) != 1:
            raise RuntimeError("nested requirement does not contain one constraint")
        return {
            "kind": "nested-requirement",
            "constraint": canonical_expression(
                children[0],
                template_parameters,
                sources,
                source_path,
                parameters,
            ),
        }
    raise RuntimeError(f"unsupported requirement {kind}")


def template_parameter_map(node):
    result = {}
    parameter_index = 0
    for child in node.get("inner", []):
        if child.get("kind") in TEMPLATE_PARAMETER_KINDS:
            name = child.get("name")
            if name:
                result[name] = parameter_index
            parameter_index += 1
    return result


def canonical_template(
    node, sources, source_path, inherited_parameters=None
):
    parameters = dict(inherited_parameters or {})
    result = []
    for child in node.get("inner", []):
        kind = child.get("kind")
        if kind not in TEMPLATE_PARAMETER_KINDS:
            continue
        index = len(result)
        if child.get("name"):
            parameters[child["name"]] = index
        parameter = {
            "kind": {
                "TemplateTypeParmDecl": "type",
                "NonTypeTemplateParmDecl": "non-type",
                "TemplateTemplateParmDecl": "template",
            }[kind],
            "pack": bool(child.get("isParameterPack", False)),
        }
        if kind == "NonTypeTemplateParmDecl":
            parameter["type"] = normalized_type(child, parameters)
        if "defaultArg" in child:
            default = child["defaultArg"]
            parameter["default"] = canonical_template_argument(
                default, parameters, sources, source_path
            )
        constraints = [
            grandchild
            for grandchild in child.get("inner", [])
            if grandchild.get("kind") in CONSTRAINT_KINDS
        ]
        if constraints:
            if len(constraints) != 1:
                raise RuntimeError("template parameter has multiple constraints")
            parameter["constraint"] = canonical_expression(
                constraints[0], parameters, sources, source_path, {}
            )
        result.append(parameter)
    return {"parameters": result}, parameters


def declaration_flags(node):
    result = {}
    for key in (
        "constexpr",
        "explicitlyDefaulted",
        "inline",
        "isBitfield",
        "isDeleted",
        "isExplicit",
        "isMutable",
        "isPure",
        "storageClass",
        "tlsKind",
        "virtual",
    ):
        if key in node:
            result[key] = node[key]
    return result


def canonical_attributes(node, sources, source_path):
    result = []
    for child in node.get("inner", []):
        kind = child.get("kind", "")
        if not kind.endswith("Attr") or child.get("isImplicit"):
            continue
        tokens = sources.tokens(child, source_path)
        attribute = {"kind": kind.removesuffix("Attr")}
        if tokens:
            attribute["tokens"] = tokens
        result.append(attribute)
    return result


def first_evaluated_value(node):
    if "value" in node:
        return node["value"]
    for child in node.get("inner", []):
        value = first_evaluated_value(child)
        if value is not None:
            return value
    return None


def canonical_parameter_defaults(
    function, template_parameters, sources, source_path
):
    defaults = []
    found = False
    for parameter in function.get("inner", []):
        if parameter.get("kind") != "ParmVarDecl":
            continue
        if parameter.get("init") is None:
            defaults.append(None)
            continue
        children = parameter.get("inner", [])
        if not children:
            raise RuntimeError("defaulted parameter has no expression")
        found = True
        defaults.append(
            canonical_expression(
                children[-1],
                template_parameters,
                sources,
                source_path,
                {},
            )
        )
    return defaults if found else None


def record_definition(node):
    if node.get("kind") == "CXXRecordDecl":
        return node if node.get("completeDefinition") else None
    if node.get("kind") == "ClassTemplateDecl":
        return next(
            (
                child
                for child in node.get("inner", [])
                if child.get("kind") == "CXXRecordDecl"
                and child.get("completeDefinition")
            ),
            None,
        )
    return None


def record_capabilities(node):
    definition = node.get("definitionData", {})
    scalar_keys = (
        "canConstDefaultInit",
        "isAggregate",
        "isEmpty",
        "isLiteral",
        "isPOD",
        "isPolymorphic",
        "isStandardLayout",
        "isTrivial",
        "isTriviallyCopyable",
    )
    member_keys = (
        "copyAssign",
        "copyCtor",
        "defaultCtor",
        "dtor",
        "moveAssign",
        "moveCtor",
    )
    result = {
        key: definition[key] for key in scalar_keys if key in definition
    }
    for key in member_keys:
        if key in definition:
            result[key] = {
                nested_key: nested_value
                for nested_key, nested_value in sorted(definition[key].items())
                if isinstance(nested_value, (bool, int, str))
            }
    return result


def canonical_constraint(
    node, template_parameters, sources, source_path
):
    constraints = [
        child
        for child in node.get("inner", [])
        if child.get("kind") in CONSTRAINT_KINDS
    ]
    if not constraints:
        return None
    if len(constraints) != 1:
        raise RuntimeError(
            f"{node.get('kind')} has multiple direct constraints"
        )
    return canonical_expression(
        constraints[0], template_parameters, sources, source_path, {}
    )
