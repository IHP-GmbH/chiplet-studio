#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 IHP GmbH
"""Every source on disk must be named in a CMakeLists, and vice versa.

The build lists its sources explicitly rather than globbing, which is the right
call (a glob makes the build depend on directory state and does not re-run
CMake when a file appears). The cost is that a new file is invisible until
someone remembers to list it, and invisible has two shapes here, both quiet:

  - a new src/ file is never compiled, so the feature is simply absent and
    nothing fails;
  - a new tests/ file is never registered, so ctest reports the same count as
    before and the test that was just written has never run.

Neither is a compile error, so only a check like this one notices. The reverse
direction is included because a listed file that has been deleted or moved
fails at configure time, which is cheaper to learn here than in the image build.

Usage:
    check_source_list.py
    check_source_list.py --self-test
"""

import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

# Only these two roots are audited. A reference outside them (the vendored
# GDS3D tree under extern/, say) is resolved so it can be seen not to be a
# stray variable, then left alone: extern/ is somebody else's source list.
AUDITED_ROOTS = ("src/", "tests/")
SOURCE_SUFFIXES = {".cpp", ".vert", ".frag"}
TOKEN = re.compile(r"[\w/\.\-\$\{\}]+\.(?:cpp|vert|frag)")
SET_LITERAL = re.compile(r"\bset\s*\(\s*(\w+)\s+([^\s()]+)\s*\)", re.I)


def strip_comments(text):
    # CMake comments run from an unquoted # to end of line. The source lists
    # here carry no quoted #, so the simple form is enough and a false strip
    # would only ever hide a reference, which this checker then reports.
    return re.sub(r"#[^\n]*", "", text)


def literal_vars(texts):
    """`set(NAME <single token>)` definitions, resolved against each other.

    Enough for the one indirection the build actually uses
    (`set(GDS3D_DIR ${CMAKE_SOURCE_DIR}/extern/GDS3D)`), and no more. Anything
    a checker cannot resolve is reported rather than skipped, so growing a
    second indirection surfaces here instead of quietly dropping paths.
    """
    variables = {"CMAKE_SOURCE_DIR": "", "CMAKE_CURRENT_SOURCE_DIR": ""}
    for text in texts:
        for name, value in SET_LITERAL.findall(text):
            variables.setdefault(name, value)
    for _ in range(4):
        for name, value in list(variables.items()):
            variables[name] = expand(value, variables)
    return variables


def expand(token, variables):
    def sub(m):
        return variables.get(m.group(1), m.group(0))
    return re.sub(r"\$\{(\w+)\}", sub, token)


def references(cmake_path, root, variables):
    """Repo-root-relative paths named by one CMakeLists."""
    text = strip_comments(cmake_path.read_text())
    here = cmake_path.parent.relative_to(root)
    out = set()
    for tok in TOKEN.findall(text):
        tok = expand(tok, variables).lstrip("/")
        if "$" in tok:
            # A path still holding a variable cannot be resolved here. Naming
            # it is better than dropping it silently.
            out.add("UNRESOLVED:" + tok)
            continue
        out.add(tok if tok.startswith(AUDITED_ROOTS) else str(here / tok))
    return out


def check(root):
    failures = []
    root = pathlib.Path(root)

    cmake_files = [root / "CMakeLists.txt", root / "tests" / "CMakeLists.txt"]
    present = [cm for cm in cmake_files if cm.exists()]
    for cm in cmake_files:
        if cm not in present:
            failures.append("%s: missing" % cm.relative_to(root))

    variables = literal_vars([strip_comments(cm.read_text()) for cm in present])
    referenced = set()
    for cm in present:
        referenced |= references(cm, root, variables)

    unresolved = sorted(r for r in referenced if r.startswith("UNRESOLVED:"))
    for u in unresolved:
        failures.append(
            "%s: source path holds an unexpanded CMake variable, so it cannot be "
            "checked. Spell it out or extend this checker." % u[len("UNRESOLVED:"):]
        )
    referenced -= set(unresolved)

    on_disk = {
        str(p.relative_to(root))
        for p in (root / "src").rglob("*")
        if p.is_file() and p.suffix in SOURCE_SUFFIXES
    }
    on_disk |= {
        str(p.relative_to(root))
        for p in (root / "tests").glob("*.cpp")
        if p.is_file()
    }

    for missing in sorted(on_disk - referenced):
        failures.append(
            "%s: on disk but named in no CMakeLists. It is not compiled, so its "
            "absence cannot fail a build." % missing
        )
    for absent in sorted(referenced - on_disk):
        if not absent.startswith(AUDITED_ROOTS):
            continue  # outside the audited roots; see AUDITED_ROOTS above
        failures.append("%s: named in a CMakeLists but not on disk" % absent)
    return failures


def self_test():
    cases = []

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "src" / "core").mkdir(parents=True)
        (root / "tests").mkdir()
        (root / "src" / "core" / "A.cpp").write_text("")
        (root / "src" / "main.cpp").write_text("")
        (root / "tests" / "test_a.cpp").write_text("")

        top = root / "CMakeLists.txt"
        tests = root / "tests" / "CMakeLists.txt"
        good_top = "add_executable(x src/main.cpp src/core/A.cpp)\n"
        good_tests = "add_executable(t test_a.cpp)\n"
        top.write_text(good_top)
        tests.write_text(good_tests)

        def expect(name, want_fail):
            got = check(root)
            cases.append((name, bool(got) == want_fail, got))

        expect("a complete source list passes", False)

        top.write_text("add_executable(x src/main.cpp)\n")
        expect("an unlisted src file fails", True)
        top.write_text(good_top)

        tests.write_text("add_executable(t)\n")
        expect("an unregistered test file fails", True)
        tests.write_text(good_tests)

        top.write_text(good_top + "add_executable(y src/core/Gone.cpp)\n")
        expect("a listed file that is not on disk fails", True)
        top.write_text(good_top)

        top.write_text(good_top + "set(S ${SOME_DIR}/x.cpp)\n")
        expect("an unresolvable source path fails", True)

    ok = True
    for name, passed, got in cases:
        print("%-40s %s" % (name, "ok" if passed else "FAILED"))
        if not passed:
            ok = False
            print("   checker returned: %r" % (got,))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()

    root = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True, text=True, check=True,
    ).stdout.strip()

    failures = check(root)
    for f in failures:
        print("FAIL: %s" % f, file=sys.stderr)
    if failures:
        return 1
    print("every source on disk is named in a CMakeLists, and vice versa")
    return 0


if __name__ == "__main__":
    sys.exit(main())
