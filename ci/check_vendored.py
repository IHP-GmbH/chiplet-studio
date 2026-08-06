#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 IHP GmbH
"""Hold src/formats/chiplet_format_io/ to the verbatim copy it declares itself to be.

CONTRIBUTING.md forbids editing the vendored reader in place: fixes belong
upstream in chiplet-spec and come back as a re-vendor. Nothing enforced that,
and the copy had silently grown a field the upstream reference did not have
while missing three upstream commits, in both directions at once, with every
suite green. This is the enforcement.

What is compared is the copy against the commit VENDORED.md itself names, not
against the upstream tip. Comparing against the tip would turn every upstream
change into a red gate here and train the reflex of bumping the declared commit
to make the red go away, which is the opposite of the property wanted. Noticing
that the declared commit has gone stale is drift, and drift belongs in the
weekly integration run, not in a merge gate.

Usage:
    check_vendored.py --spec-dir <chiplet-spec checkout>
    check_vendored.py --print-commit          # the sha VENDORED.md declares
    check_vendored.py --self-test             # prove the checks can fail
"""

import argparse
import hashlib
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

VENDOR_DIR = pathlib.Path("src/formats/chiplet_format_io")
UPSTREAM_SUBDIR = "reference/cpp"
VENDORED_SUFFIXES = {".hpp", ".cpp"}


def parse_vendored_md(path):
    """Return (commit, declared file list) from a VENDORED.md."""
    text = path.read_text()

    m = re.search(r"^- \*\*Commit:\*\* `([0-9a-f]{40})`", text, re.M)
    if not m:
        raise SystemExit(
            "%s: no `- **Commit:** <40 hex>` line. The vendoring commit is what "
            "the copy is compared against; without it there is nothing to "
            "compare to." % path
        )
    commit = m.group(1)

    declared = re.findall(r"^- `([^`]+\.(?:hpp|cpp))`", text, re.M)
    if not declared:
        raise SystemExit(
            "%s: no declared file list (lines of the form ``- `path.cpp` ``)." % path
        )
    return commit, sorted(declared)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(root, spec_dir):
    """Return a list of failure strings; empty means the copy is verbatim."""
    failures = []
    vendor = root / VENDOR_DIR
    commit, declared = parse_vendored_md(vendor / "VENDORED.md")

    on_disk = sorted(
        str(p.relative_to(vendor))
        for p in vendor.rglob("*")
        if p.is_file() and p.suffix in VENDORED_SUFFIXES
    )
    if on_disk != declared:
        failures.append(
            "VENDORED.md declares %s but the directory holds %s. A file that is "
            "vendored without being declared is a copy nobody knows to re-sync."
            % (declared, on_disk)
        )

    upstream = pathlib.Path(spec_dir) / UPSTREAM_SUBDIR
    for rel in sorted(set(declared) | set(on_disk)):
        ours, theirs = vendor / rel, upstream / rel
        if not ours.exists():
            failures.append("%s: declared in VENDORED.md, absent from the tree" % rel)
            continue
        if not theirs.exists():
            failures.append(
                "%s: not present in %s at %s. Either it was added here in place "
                "(forbidden; upstream it instead) or the declared commit is wrong."
                % (rel, UPSTREAM_SUBDIR, commit[:7])
            )
            continue
        if sha256(ours) != sha256(theirs):
            failures.append(
                "%s: differs from chiplet-spec@%s. Do not edit the vendored copy; "
                "change it upstream and re-vendor, updating VENDORED.md."
                % (rel, commit[:7])
            )
    return failures


def self_test():
    """Every check above, made to fail on purpose.

    A checker with no negative test is indistinguishable from a checker that
    returns 0 unconditionally, and this one runs in a gate.
    """
    src_root = pathlib.Path(__file__).resolve().parent.parent
    commit, declared = parse_vendored_md(src_root / VENDOR_DIR / "VENDORED.md")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        root, spec = tmp / "studio", tmp / "spec"
        (root / VENDOR_DIR).mkdir(parents=True)
        (spec / UPSTREAM_SUBDIR).mkdir(parents=True)

        shutil.copy(src_root / VENDOR_DIR / "VENDORED.md", root / VENDOR_DIR)
        for rel in declared:
            for base in (root / VENDOR_DIR, spec / UPSTREAM_SUBDIR):
                (base / rel).parent.mkdir(parents=True, exist_ok=True)
                (base / rel).write_text("// %s\n" % rel)

        cases = []

        def expect(name, want_fail):
            got = check(root, spec)
            ok = bool(got) == want_fail
            cases.append((name, ok, got))
            return ok

        expect("identical trees pass", False)

        victim = root / VENDOR_DIR / declared[0]
        original = victim.read_text()
        victim.write_text(original + "// edited in place\n")
        expect("an in-place edit fails", True)
        victim.write_text(original)

        stray = root / VENDOR_DIR / "src" / "extra_helper.cpp"
        stray.parent.mkdir(parents=True, exist_ok=True)
        stray.write_text("// undeclared\n")
        expect("an undeclared vendored file fails", True)
        stray.unlink()

        (spec / UPSTREAM_SUBDIR / declared[0]).unlink()
        expect("a file missing upstream fails", True)

        ok = True
        for name, passed, got in cases:
            print("%-40s %s" % (name, "ok" if passed else "FAILED"))
            if not passed:
                ok = False
                print("   checker returned: %r" % (got,))
        return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--spec-dir", help="a chiplet-spec checkout at the declared commit")
    ap.add_argument("--print-commit", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    root = pathlib.Path(
        subprocess.run(
            ["git", "rev-parse", "--show-toplevel"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
    )

    if args.self_test:
        return self_test()

    if args.print_commit:
        print(parse_vendored_md(root / VENDOR_DIR / "VENDORED.md")[0])
        return 0

    if not args.spec_dir:
        ap.error("--spec-dir is required unless --print-commit or --self-test")

    failures = check(root, args.spec_dir)
    for f in failures:
        print("FAIL: %s" % f, file=sys.stderr)
    if failures:
        return 1
    print("vendored chiplet_format_io is verbatim against the declared commit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
