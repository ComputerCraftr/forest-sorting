#!/usr/bin/env python3

import tempfile
from pathlib import Path

from clang_ast import expand_response_files, reconstruct_semantic_arguments


def expect_equal(actual, expected):
    if actual != expected:
        raise AssertionError(f"expected {expected!r}, got {actual!r}")


def expect_failure(callback, text):
    try:
        callback()
    except RuntimeError as error:
        if text not in str(error):
            raise AssertionError(f"missing {text!r} in {error!r}") from error
        return
    raise AssertionError(f"expected failure containing {text!r}")


def reconstruct(arguments):
    return reconstruct_semantic_arguments(
        arguments, "/work/source.cpp", "/work"
    )


def test_gcc_command():
    expect_equal(
        reconstruct(
            [
                "g++",
                "-Iinclude",
                "-isystem",
                "/system",
                "-DVALUE=1",
                "-std=c++20",
                "-O3",
                "-Wall",
                "-c",
                "/work/source.cpp",
                "-o",
                "source.o",
            ]
        ),
        [
            "-Iinclude",
            "-isystem",
            "/system",
            "-DVALUE=1",
            "-std=c++20",
        ],
    )


def test_apple_command():
    expect_equal(
        reconstruct(
            [
                "clang++",
                "-arch",
                "arm64",
                "-isysroot",
                "/sdk",
                "-mmacosx-version-min=15.0",
                "-stdlib=libc++",
                "-g",
                "/work/source.cpp",
            ]
        ),
        [
            "-arch",
            "arm64",
            "-isysroot",
            "/sdk",
            "-mmacosx-version-min=15.0",
            "-stdlib=libc++",
        ],
    )


def test_split_joined_and_modules():
    expect_equal(
        reconstruct(
            [
                "clang++",
                "-x",
                "c++",
                "-U",
                "OLD",
                "-iquotequoted",
                "-fmodules",
                "-fmodule-map-file=module.modulemap",
                "-fprebuilt-module-path",
                "prebuilt",
                "-Xclang",
                "-fmodule-file",
                "-Xclang",
                "named=module.pcm",
                "-pthread",
                "/work/source.cpp",
            ]
        ),
        [
            "-x",
            "c++",
            "-U",
            "OLD",
            "-iquotequoted",
            "-fmodules",
            "-fmodule-map-file=module.modulemap",
            "-fprebuilt-module-path",
            "prebuilt",
            "-Xclang",
            "-fmodule-file",
            "-Xclang",
            "named=module.pcm",
            "-pthread",
        ],
    )


def test_response_files():
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        nested = root / "nested.rsp"
        outer = root / "outer.rsp"
        nested.write_text("-DVALUE=1 -I include", encoding="utf-8")
        outer.write_text(f"@{nested.name} -std=c++20", encoding="utf-8")
        expect_equal(
            expand_response_files([f"@{outer.name}"], root),
            ["-DVALUE=1", "-I", "include", "-std=c++20"],
        )

        cycle_a = root / "a.rsp"
        cycle_b = root / "b.rsp"
        cycle_a.write_text("@b.rsp", encoding="utf-8")
        cycle_b.write_text("@a.rsp", encoding="utf-8")
        expect_failure(
            lambda: expand_response_files(["@a.rsp"], root),
            "response-file cycle",
        )

        malformed = root / "malformed.rsp"
        malformed.write_text('"unterminated', encoding="utf-8")
        expect_failure(
            lambda: expand_response_files(["@malformed.rsp"], root),
            "cannot parse response file",
        )
        expect_failure(
            lambda: expand_response_files(["@missing.rsp"], root),
            "does not exist",
        )

        nested_files = [root / f"depth-{index}.rsp" for index in range(18)]
        for index, response in enumerate(nested_files[:-1]):
            response.write_text(
                f"@{nested_files[index + 1].name}", encoding="utf-8"
            )
        nested_files[-1].write_text("-DEND=1", encoding="utf-8")
        expect_failure(
            lambda: expand_response_files([f"@{nested_files[0].name}"], root),
            "nesting exceeds",
        )


def test_unknown_inputs():
    expect_failure(
        lambda: reconstruct(["g++", "-funknown-semantic", "/work/source.cpp"]),
        "unrecognized parsing option",
    )
    expect_failure(
        lambda: reconstruct(["g++", "other.cpp", "/work/source.cpp"]),
        "unrecognized positional",
    )
    expect_failure(
        lambda: reconstruct(["g++", "-Wp,-DVALUE=1", "/work/source.cpp"]),
        "represented explicitly",
    )


def main():
    test_gcc_command()
    test_apple_command()
    test_split_joined_and_modules()
    test_response_files()
    test_unknown_inputs()


if __name__ == "__main__":
    main()
