#!/usr/bin/env python3

import argparse
import json
import subprocess
import sys
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--python", required=True)
    parser.add_argument("--audit-script", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--compile-commands", required=True)
    parser.add_argument("--source", required=True)
    parser.add_argument("--object", required=True)
    parser.add_argument("--report", required=True)
    return parser.parse_args()


def main():
    args = parse_args()
    completed = subprocess.run(
        [
            args.python,
            args.audit_script,
            "--clang",
            args.clang,
            "--compile-commands",
            args.compile_commands,
            "--source",
            args.source,
            "--object",
            args.object,
            "--runner",
            "missingExpectedRunner",
            "--report",
            args.report,
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode == 0:
        raise RuntimeError("negative app linkage fixture unexpectedly passed")
    report = json.loads(Path(args.report).read_text(encoding="utf-8"))
    failures = {
        (failure["kind"], failure["name"]) for failure in report["failures"]
    }
    expected = {
        ("FunctionDecl", "accidentalGlobal"),
        ("FunctionDecl", "forest_sorting::accidentalExport"),
        (
            "FunctionDecl",
            "forest_sorting::benchmark_support::accidentalExport",
        ),
        ("CXXMethodDecl", "forest_sorting::AccidentalMember::method"),
        ("VarDecl", "forest_sorting::AccidentalMember::value"),
        ("FunctionDecl", "accidentalFunctionTemplate"),
        ("VarDecl", "accidentalVariableTemplate"),
        ("ClassTemplateSpecializationDecl", "AccidentalClassTemplate"),
    }
    missing = sorted(expected - failures)
    if missing:
        raise RuntimeError(
            f"app linkage audit missed seeded definitions {missing}:\n"
            + completed.stderr
        )
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (RuntimeError, json.JSONDecodeError) as error:
        print(f"negative app linkage test failed: {error}", file=sys.stderr)
        sys.exit(1)
