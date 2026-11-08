#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 IHP GmbH
"""Hold src/formats/chiplet_format_io/ to the copy VENDORED.md records.

CONTRIBUTING.md forbids editing the vendored reader in place: fixes belong
upstream in chiplet-system-spec and come back as a re-vendor. Nothing enforced
that, and the copy had silently grown a field the upstream reference did not
have while missing three upstream commits, in both directions at once, with
every suite green. This is the enforcement.

VENDORED.md records a content digest of the vendored files, which anyone can
recompute with sha256sum, and no upstream commit id. This check needs no
network and no upstream checkout: the declared file list must be exactly the
files on disk, and those files must hash to the recorded digest.

That is all it proves. The copy matches the note; it does not prove the note
matches upstream. An edit that also rewrites the digest passes here, so a
change to the digest line is the thing a reviewer must hold to a real
re-vendor. The comparison with upstream bytes is the contracts case
test_vendored_reader_is_in_the_pinned_export, which needs a chiplet-system-spec
export and skips where the export does not hold these bytes.

Usage:
    check_vendored.py                  # the copy against its recorded digest
    check_vendored.py --print-files    # the declared files, one per line
    check_vendored.py --print-digest   # the recorded digest
    check_vendored.py --self-test      # prove each check can fail
"""

import argparse
import hashlib
import pathlib
import re
import subprocess
import sys
import tempfile

VENDOR_DIR = pathlib.Path("src/formats/chiplet_format_io")
VENDORED_SUFFIXES = {".hpp", ".cpp"}
DIGEST_LINE = re.compile(r"^- \*\*Content digest:\*\* `([0-9a-f]{64})`", re.M)


def parse_vendored_md(path):
    """Return (recorded digest, declared file list) from a VENDORED.md."""
    text = path.read_text()

    m = DIGEST_LINE.search(text)
    if not m:
        raise SystemExit(
            "%s: no `- **Content digest:** <64 hex>` line. The digest is what "
            "the copy is checked against; without it there is nothing to "
            "check." % path
        )
    digest = m.group(1)

    declared = re.findall(r"^- `([^`]+\.(?:hpp|cpp))`", text, re.M)
    if not declared:
        raise SystemExit(
            "%s: no declared file list (lines of the form ``- `path.cpp` ``)." % path
        )
    return digest, sorted(declared)


def content_digest(base, files):
    """`cd <base> && sha256sum <files sorted by path> | sha256sum`."""
    listing = "".join(
        "%s  %s\n" % (hashlib.sha256((base / rel).read_bytes()).hexdigest(), rel)
        for rel in sorted(files)
    )
    return hashlib.sha256(listing.encode()).hexdigest()


def check(root):
    """Return a list of failure strings; empty means the copy matches the note."""
    vendor = root / VENDOR_DIR
    digest, declared = parse_vendored_md(vendor / "VENDORED.md")

    on_disk = sorted(
        str(p.relative_to(vendor))
        for p in vendor.rglob("*")
        if p.is_file() and p.suffix in VENDORED_SUFFIXES
    )
    if on_disk != declared:
        return [
            "VENDORED.md declares %s but the directory holds %s. A file that is "
            "vendored without being declared is a copy nobody knows to re-sync."
            % (declared, on_disk)
        ]

    # The computed digest is left out of the message on purpose: printed next
    # to "update VENDORED.md" it reads as the value to paste, which turns an
    # in-place edit into a passing one.
    if content_digest(vendor, declared) != digest:
        return [
            "the vendored files do not hash to the digest VENDORED.md records. "
            "Do not edit the vendored copy; change it upstream and re-vendor."
        ]
    return []


def self_test():
    """Every check above, made to fail on purpose, each by its own message.

    A checker with no negative test is indistinguishable from a checker that
    returns 0 unconditionally, and this one runs in a gate.
    """
    src_root = pathlib.Path(__file__).resolve().parent.parent
    _, declared = parse_vendored_md(src_root / VENDOR_DIR / "VENDORED.md")

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        vendor = root / VENDOR_DIR
        vendor.mkdir(parents=True)
        for rel in declared:
            (vendor / rel).parent.mkdir(parents=True, exist_ok=True)
            (vendor / rel).write_text("// %s\n" % rel)

        # The real note, re-pointed at the stand-in files' digest, so the
        # parser under test is the one the gate runs on the real note.
        note = (src_root / VENDOR_DIR / "VENDORED.md").read_text()
        note, n = DIGEST_LINE.subn(
            "- **Content digest:** `%s`" % content_digest(vendor, declared), note)
        assert n == 1, "VENDORED.md has %d digest lines, expected 1" % n
        (vendor / "VENDORED.md").write_text(note)

        cases = []

        def expect(name, fragment):
            """fragment None: must pass; else must fail with that in the message."""
            got = check(root)
            ok = not got if fragment is None else any(fragment in f for f in got)
            cases.append((name, ok, got))

        expect("the recorded copy passes", None)

        victim = vendor / declared[0]
        original = victim.read_text()
        victim.write_text(original + "// edited in place\n")
        expect("an in-place edit fails", "do not hash to the digest")
        victim.write_text(original)

        stray = vendor / "src" / "extra_helper.cpp"
        stray.parent.mkdir(parents=True, exist_ok=True)
        stray.write_text("// undeclared\n")
        expect("an undeclared vendored file fails", "vendored without being declared")
        stray.unlink()

        victim.unlink()
        expect("a declared file that is absent fails", "vendored without being declared")
        victim.write_text(original)

        expect("the restored copy passes again", None)

        ok = True
        for name, passed, got in cases:
            print("%-40s %s" % (name, "ok" if passed else "FAILED"))
            if not passed:
                ok = False
                print("   checker returned: %r" % (got,))
        return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--print-files", action="store_true")
    ap.add_argument("--print-digest", action="store_true")
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

    digest, declared = parse_vendored_md(root / VENDOR_DIR / "VENDORED.md")
    if args.print_files:
        print("\n".join(declared))
        return 0
    if args.print_digest:
        print(digest)
        return 0

    failures = check(root)
    for f in failures:
        print("FAIL: %s" % f, file=sys.stderr)
    if failures:
        return 1
    print("vendored chiplet_format_io matches the digest VENDORED.md records")
    return 0


if __name__ == "__main__":
    sys.exit(main())
