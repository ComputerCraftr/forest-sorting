#!/usr/bin/env python3

import json
import re
import shlex
import subprocess
from pathlib import Path

MAX_RESPONSE_DEPTH = 16

_SEMANTIC_SPLIT_OPTIONS = {
    "-D",
    "-F",
    "-I",
    "-U",
    "-arch",
    "-fmodule-file",
    "-fmodule-map-file",
    "-fmodules-cache-path",
    "-fprebuilt-module-path",
    "-iframework",
    "-imacros",
    "-include",
    "-idirafter",
    "-iquote",
    "-isystem",
    "-isysroot",
    "-ivfsoverlay",
    "-resource-dir",
    "-stdlib",
    "-std",
    "-target",
    "-x",
    "--sysroot",
    "--target",
}

_SEMANTIC_JOINED_PREFIXES = (
    "-D",
    "-F",
    "-I",
    "-U",
    "-fmodule-file=",
    "-fmodule-map-file=",
    "-fmodules-cache-path=",
    "-fprebuilt-module-path=",
    "-iframework",
    "-imacros",
    "-include",
    "-idirafter",
    "-iquote",
    "-isystem",
    "-isysroot",
    "-ivfsoverlay",
    "-resource-dir=",
    "-stdlib=",
    "-std=",
    "-target=",
    "--sysroot=",
    "--target=",
)

_SEMANTIC_FLAGS = {
    "-fblocks",
    "-fchar8_t",
    "-fcoroutines",
    "-fdelayed-template-parsing",
    "-fexceptions",
    "-fexperimental-library",
    "-fmodules",
    "-fmodules-local-submodule-visibility",
    "-fmodules-ts",
    "-fms-compatibility",
    "-fms-extensions",
    "-fno-blocks",
    "-fno-char8_t",
    "-fno-exceptions",
    "-fno-modules",
    "-fno-rtti",
    "-fno-sized-deallocation",
    "-fno-threadsafe-statics",
    "-fno-use-cxa-atexit",
    "-frtti",
    "-fsized-deallocation",
    "-fthreadsafe-statics",
    "-fuse-cxa-atexit",
    "-pthread",
}

_DROP_SPLIT_OPTIONS = {
    "-MF",
    "-MJ",
    "-MQ",
    "-MT",
    "-Xassembler",
    "-Xlinker",
    "-dependency-file",
    "-gcc-toolchain",
    "-mllvm",
    "-o",
    "-serialize-diagnostics",
}

_DROP_FLAGS = {
    "-M",
    "-MD",
    "-MG",
    "-MM",
    "-MMD",
    "-MP",
    "-S",
    "-c",
    "-pipe",
}

_DROP_PREFIXES = (
    "-O",
    "-W",
    "-falign-",
    "-fcolor-diagnostics",
    "-fcoverage",
    "-fdebug",
    "-fdiagnostics",
    "-ffile-prefix-map=",
    "-ffunction-sections",
    "-fmacro-prefix-map=",
    "-fno-color-diagnostics",
    "-fno-diagnostics",
    "-fno-omit-frame-pointer",
    "-fomit-frame-pointer",
    "-fprofile",
    "-fsanitize",
    "-fstack-",
    "-fuse-ld=",
    "-g",
)

_CC1_SEMANTIC_FLAGS = {
    "-fmodules",
    "-fmodules-local-submodule-visibility",
    "-fmodules-ts",
    "-fno-modules",
}

_CC1_SEMANTIC_SPLIT_OPTIONS = {
    "-fmodule-file",
    "-fmodule-map-file",
    "-fmodules-cache-path",
    "-fprebuilt-module-path",
}


def verify_manifest_clang(clang):
    completed = subprocess.run(
        [clang, "--version"], check=True, capture_output=True, text=True
    )
    first_line = completed.stdout.splitlines()[0]
    match = re.search(r"(?:clang version|version) ([0-9]+)", first_line)
    if (
        "Apple clang" in first_line
        or match is None
        or int(match.group(1)) != 22
    ):
        raise RuntimeError(
            "AST enforcement requires upstream Clang 22; found " + first_line
        )


def _response_tokens(path, directory, active, depth):
    resolved = path.resolve()
    if depth > MAX_RESPONSE_DEPTH:
        raise RuntimeError(
            f"response-file nesting exceeds {MAX_RESPONSE_DEPTH}: {resolved}"
        )
    if resolved in active:
        chain = " -> ".join(str(item) for item in [*active, resolved])
        raise RuntimeError(f"response-file cycle: {chain}")
    if not resolved.is_file():
        raise RuntimeError(f"response file does not exist: {resolved}")
    try:
        contents = resolved.read_text(encoding="utf-8")
        tokens = shlex.split(contents, posix=True)
    except (OSError, UnicodeError, ValueError) as error:
        raise RuntimeError(
            f"cannot parse response file {resolved}: {error}"
        ) from error
    return expand_response_files(
        tokens, directory, (*active, resolved), depth + 1
    )


def expand_response_files(tokens, directory, active=(), depth=0):
    result = []
    for token in tokens:
        if not token.startswith("@"):
            result.append(token)
            continue
        response = token[1:]
        if not response:
            raise RuntimeError("empty response-file argument")
        path = Path(response)
        if not path.is_absolute():
            path = Path(directory) / path
        result.extend(_response_tokens(path, directory, active, depth))
    return result


def _same_path(token, source, directory):
    path = Path(token)
    if not path.is_absolute():
        path = Path(directory) / path
    try:
        return path.resolve() == Path(source).resolve()
    except OSError:
        return False


def _joined_semantic_option(token):
    for prefix in _SEMANTIC_JOINED_PREFIXES:
        if token.startswith(prefix) and token != prefix:
            return True
    return False


def _is_semantic_target_flag(token):
    return token.startswith(
        (
            "-m",
            "-mmacosx-version-min=",
            "-miphoneos-version-min=",
            "-mtvos-version-min=",
            "-mwatchos-version-min=",
        )
    )


def reconstruct_semantic_arguments(arguments, source, directory):
    if not arguments:
        raise RuntimeError("compile command is empty")
    expanded = expand_response_files(arguments[1:], directory)
    semantic = []
    index = 0
    while index < len(expanded):
        token = expanded[index]
        if token in _SEMANTIC_SPLIT_OPTIONS:
            if index + 1 >= len(expanded):
                raise RuntimeError(f"missing argument for semantic option {token}")
            semantic.extend((token, expanded[index + 1]))
            index += 2
            continue
        if token in _DROP_SPLIT_OPTIONS:
            if index + 1 >= len(expanded):
                raise RuntimeError(f"missing argument for build option {token}")
            index += 2
            continue
        if token == "-Xclang":
            if index + 1 >= len(expanded):
                raise RuntimeError("missing argument for -Xclang")
            cc1_option = expanded[index + 1]
            if cc1_option in _CC1_SEMANTIC_FLAGS:
                semantic.extend((token, cc1_option))
                index += 2
                continue
            if cc1_option in _CC1_SEMANTIC_SPLIT_OPTIONS:
                if (
                    index + 3 >= len(expanded)
                    or expanded[index + 2] != "-Xclang"
                ):
                    raise RuntimeError(
                        f"missing cc1 argument for {cc1_option}"
                    )
                semantic.extend(expanded[index : index + 4])
                index += 4
                continue
            raise RuntimeError(f"unrecognized cc1 parsing option: {cc1_option}")
        if token in _SEMANTIC_FLAGS or _joined_semantic_option(token):
            semantic.append(token)
            index += 1
            continue
        if _is_semantic_target_flag(token):
            semantic.append(token)
            index += 1
            continue
        if token.startswith("-Wp,"):
            raise RuntimeError(
                f"preprocessor option must be represented explicitly: {token}"
            )
        if token in _DROP_FLAGS or token.startswith(_DROP_PREFIXES):
            index += 1
            continue
        if token.startswith("-"):
            raise RuntimeError(f"unrecognized parsing option: {token}")
        if _same_path(token, source, directory):
            index += 1
            continue
        raise RuntimeError(f"unrecognized positional compile input: {token}")
    return semantic


def _compile_entry(database_path, source_path, object_path=None):
    database = json.loads(Path(database_path).read_text(encoding="utf-8"))
    source = Path(source_path).resolve()
    expected_object = (
        Path(object_path).resolve() if object_path is not None else None
    )
    for item in database:
        directory = Path(item.get("directory", ".")).resolve()
        item_file = Path(item["file"])
        if not item_file.is_absolute():
            item_file = directory / item_file
        if item_file.resolve() != source:
            continue
        if expected_object is not None:
            output = item.get("output")
            if output is None:
                continue
            output_path = Path(output)
            if not output_path.is_absolute():
                output_path = directory / output_path
            if output_path.resolve() != expected_object:
                continue
        arguments = item.get("arguments")
        if arguments is None:
            arguments = shlex.split(item["command"], posix=True)
        return arguments, directory
    suffix = f" and object {expected_object}" if expected_object else ""
    raise RuntimeError(f"compile command not found for {source}{suffix}")


def build_ast_command(database_path, source_path, clang, object_path=None):
    arguments, directory = _compile_entry(
        database_path, source_path, object_path
    )
    semantic = reconstruct_semantic_arguments(
        arguments, source_path, directory
    )
    return (
        [
            clang,
            *semantic,
            "-Xclang",
            "-ast-dump=json",
            "-fsyntax-only",
            str(Path(source_path).resolve()),
        ],
        str(directory),
    )


def load_ast(database_path, source_path, clang, object_path=None):
    command, directory = build_ast_command(
        database_path, source_path, clang, object_path
    )
    completed = subprocess.run(
        command,
        cwd=directory,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        rendered = " ".join(shlex.quote(part) for part in command)
        raise RuntimeError(
            f"Clang AST command failed:\n{rendered}\n{completed.stderr}"
        )
    return json.loads(completed.stdout)
