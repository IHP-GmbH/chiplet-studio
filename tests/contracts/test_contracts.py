# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 IHP GmbH
"""The compiler-free contracts, as pytest cases for a host with no build.

These are the checks the hosted `contracts` job in .github/workflows/ci.yml
runs, and the only ones in this repository that need no compiler, no Qt and no
KLayout. Each case runs its check the way that job does, through this
interpreter or bash, from the checkout root, and fails with the check's own
output.

An exit status of 0 is not taken alone where a check could reach it without
checking anything: a self-test must name at least one case and fail none, and
the launcher test must report how many assertions passed.

The last case reads a sibling, so it carries `needs_sibling_sources`: the
vendored reader's bytes looked up in the chiplet-system-spec export at
$HCDP_ROOT/chiplet-spec. It skips where the export does not hold them.
"""

import os
import pathlib
import re
import subprocess
import sys

import pytest

REPO = pathlib.Path(__file__).resolve().parents[2]
VENDOR = REPO / "src/formats/chiplet_format_io"
READER_HEADER = "include/chiplet_format_io/chiplet_format_io.hpp"


def run(argv, cwd=REPO):
    return subprocess.run(argv, cwd=cwd, capture_output=True, text=True)


def explain(proc):
    return "exit %d\n--- stdout\n%s--- stderr\n%s" % (proc.returncode, proc.stdout, proc.stderr)


def assert_self_test(proc):
    assert proc.returncode == 0, explain(proc)
    cases = [line for line in proc.stdout.splitlines() if line.rstrip().endswith(" ok")]
    assert cases, "the self-test named no case:\n" + explain(proc)
    assert "FAILED" not in proc.stdout, explain(proc)


def test_source_list_checker_can_fail():
    assert_self_test(run([sys.executable, "ci/check_source_list.py", "--self-test"]))


def test_every_source_on_disk_is_listed_in_a_cmakelists():
    proc = run([sys.executable, "ci/check_source_list.py"])
    assert proc.returncode == 0, explain(proc)


def test_vendored_checker_can_fail():
    assert_self_test(run([sys.executable, "ci/check_vendored.py", "--self-test"]))


def test_vendored_copy_matches_its_recorded_digest():
    proc = run([sys.executable, "ci/check_vendored.py"])
    assert proc.returncode == 0, explain(proc)


def test_demo_launchers_pass_the_flags_they_have_to_pass():
    proc = run(["bash", "tests/shell/test_demo_entrypoints.sh"])
    assert proc.returncode == 0, explain(proc)
    summary = re.search(r"^passed (\d+), failed (\d+)$", proc.stdout, re.M)
    assert summary, "no summary line:\n" + explain(proc)
    assert int(summary.group(1)) > 0 and summary.group(2) == "0", explain(proc)


def git_run(repo, *args):
    return run(["git", "-C", str(repo), *args])


def git(repo, *args):
    proc = git_run(repo, *args)
    assert proc.returncode == 0, explain(proc)
    return proc.stdout.strip()


def reader_release(header):
    # Upstream's own pattern for the line (conformance/test_version_policy.py).
    found = re.search(r'READER_RELEASE\s*=\s*"([^"]+)"', header)
    assert found, "no READER_RELEASE in the header"
    release = found.group(1)
    assert re.fullmatch(r"\d+(\.\d+)*", release), (
        "READER_RELEASE %r is not dotted numbers; this case cannot order it" % release)
    return tuple(int(part) for part in release.split("."))


@pytest.mark.needs_sibling_sources
def test_vendored_reader_is_in_the_pinned_export():
    root = os.environ.get("HCDP_ROOT")
    if not root:
        pytest.skip("HCDP_ROOT unset: no chiplet-system-spec export to "
                    "compare with")
    spec = pathlib.Path(root) / "chiplet-spec"
    if not spec.is_dir():
        pytest.skip("no chiplet-system-spec export under HCDP_ROOT (%s)" % spec)
    head = git(spec, "rev-parse", "HEAD")

    # The vendored reader may lag the pinned spec but never lead it. This
    # reads both headers, so it runs on any export.
    vendored = reader_release((VENDOR / READER_HEADER).read_text())
    pinned = reader_release(git(spec, "show", "HEAD:reference/cpp/" + READER_HEADER))
    assert vendored <= pinned, (
        "the vendored reader's release %s is newer than the pinned "
        "chiplet-system-spec's %s at %s" % (vendored, pinned, head))

    # The vendored bytes, looked up as git blobs in the export's object store.
    # VENDORED.md records a digest, not a commit id, so there is no commit to
    # fetch; a blob present in the export is upstream content byte for byte,
    # whether HEAD reaches it or an extra fetched commit carries it. History is
    # not walked: a shallow export cannot show it, and a lagging copy is
    # allowed, so where it is absent the case skips and the offline digest
    # check (ci/check_vendored.py) is the only one that ran.
    listed = run([sys.executable, "ci/check_vendored.py", "--print-files"])
    assert listed.returncode == 0, explain(listed)
    files = listed.stdout.split()
    assert files, "check_vendored.py --print-files named no file"
    absent = [rel for rel in files
              if git_run(spec, "cat-file", "-e", "%s^{blob}" % git(
                  REPO, "hash-object", "--no-filters", str(VENDOR / rel))).returncode != 0]
    if absent:
        digest = run([sys.executable, "ci/check_vendored.py", "--print-digest"])
        pytest.skip("the chiplet-system-spec export at %s holds no copy of the "
                    "vendored reader %s (content digest %s; absent: %s)"
                    % (head, ".".join(map(str, vendored)), digest.stdout.strip(),
                       ", ".join(absent)))
