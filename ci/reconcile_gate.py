#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 IHP GmbH
"""Reconcile what ctest was ASKED to run against what it REPORTS having run.

A gate that greps "100% tests passed, 0 tests failed out of N" out of a log is
green for an empty suite, for a suite that registered fewer tests than it should
have, and for one where the case that matters self-skipped. The line is
self-consistent in all three: ctest says out of N, and N is whatever it found.

So the summary line is not read here at all. Two independent artifacts are:

  --inventory   `ctest --show-only=json-v1`, the tests ctest RESOLVED from the
                build. This is the ask.
  --junit       `ctest --output-junit`, one <testcase> per test with its status.
                This is the outcome.

The reconciliation is exact SET IDENTITY between the two, both non-empty. A test
in the inventory and absent from the JUnit did not run and nothing else would
have said so; a test in the JUnit and absent from the inventory means the two
artifacts came from different runs and neither can be trusted. Duplicate names
in either are rejected: set identity over a bag hides a duplicate that masks a
missing test.

Skips are failures by default. In this suite a skip is an input the invocation
did not bind, not a test that cannot run; nine of ten historical "known skips"
were exactly that, and two of them were the tests certifying save safety.

--require names cases that MUST be present and passed. Set identity says the run
was complete with respect to itself; it cannot say the build registered the
tests it should have. A file that was never added to a CMakeLists is absent from
both artifacts and reconciles perfectly.

Usage:
    reconcile_gate.py --inventory tests.json --junit results.xml [--require FILE]
                      [--allow-skips]
    reconcile_gate.py --self-test
"""

import argparse
import json
import sys
import xml.etree.ElementTree as ET


def inventory_names(path):
    with open(path) as handle:
        data = json.load(handle)
    return [test["name"] for test in data.get("tests", [])]


# The producer is pinned, and the pin is what makes an absent counter readable
# rather than suspicious. ctest 3.22.1, as shipped in the image this gate runs,
# writes status="run" on a case that executed and a <testsuite> header carrying
# tests, failures, disabled and skipped. It does NOT write an errors attribute.
#
# The first version of this file required errors to be present and would have
# failed every real run of the gate it exists to judge: not a missed defect, a
# manufactured one. The profile below is the fix, and it is a profile and not a
# relaxation. The four counters this producer does emit stay REQUIRED, since a
# report without them is one this reconciler cannot check against itself. errors
# is optional only in the sense that its ABSENCE is not a finding; any value it
# does carry must still be zero, so a producer that grows the attribute later,
# or a different producer that has always written it, is checked exactly as
# before. What the absence costs is covered by a per-case walk for <error>
# elements, which is now the only thing that can see an errored case.
PASS_STATUS = "run"
REQUIRED_COUNTERS = ("tests", "failures", "disabled", "skipped")
OPTIONAL_COUNTERS = ("errors",)


def junit_cases(path):
    """Return ({name: status}, duplicates, suite counters).

    A case is a PASS only when it says status="run" explicitly AND carries no
    adverse child element. A missing or empty status is "unknown", never a pass:
    treating absence as success is how a report from a tool that did not run the
    tests reads as green.
    """
    root = ET.parse(path).getroot()
    cases = {}
    duplicates = []
    for case in root.iter("testcase"):
        name = case.get("name")
        if case.find("skipped") is not None:
            status = "skip"
        elif case.find("failure") is not None:
            status = "fail"
        elif case.find("error") is not None:
            status = "error"
        else:
            attr = (case.get("status") or "").strip().lower()
            status = "pass" if attr == PASS_STATUS else ("unknown:%s" % (attr or "absent"))
        if name in cases:
            duplicates.append(name)
        cases[name] = status

    # Suite counters, read from wherever the <testsuite> element is: ctest emits
    # a bare <testsuite> root, other producers wrap it in <testsuites>.
    #
    # PER SUITE, deliberately, one entry per <testsuite> with None for an
    # attribute that suite does not carry. The first version summed them and
    # collapsed the whole key to None as soon as ANY suite omitted it, which was
    # harmless while a None was rejected outright and became a hole the moment
    # an absent errors attribute was allowed: one suite omitting errors turned
    # the check off for every other suite in the same report, errors="1"
    # included. An aggregate cannot express "absent here, 1 over there", so the
    # aggregate is not what is kept.
    #
    # int() is left to raise on a non-numeric counter. An unreadable report is a
    # failed run here, and the caller turns the exception into one.
    suites = [root] if root.tag == "testsuite" else list(root.iter("testsuite"))
    counters = {}
    for key in ("tests", "failures", "errors", "disabled", "skipped"):
        counters[key] = [None if s.get(key) is None else int(s.get(key))
                         for s in suites]
    return cases, duplicates, counters


def reconcile(inventory, cases, duplicates, required, allow_skips, counters=None):
    """Return a list of failure strings; empty means the run reconciles."""
    problems = []
    counters = {} if counters is None else counters

    if not inventory:
        problems.append(
            "the ctest inventory is EMPTY. A suite that registered no tests "
            "reports '0 tests failed out of 0' and reads as green.")
    if not cases:
        problems.append("the JUnit report contains no <testcase> elements.")
    if duplicates:
        problems.append(
            "duplicate test names in the JUnit report: %s. Set identity over a "
            "bag can hide a missing test." % sorted(set(duplicates)))

    seen_inventory = set()
    inventory_dupes = sorted({n for n in inventory
                              if n in seen_inventory or seen_inventory.add(n)})
    if inventory_dupes:
        problems.append("duplicate test names in the inventory: %s" % inventory_dupes)

    asked, ran = set(inventory), set(cases)
    missing = sorted(asked - ran)
    extra = sorted(ran - asked)
    if missing:
        problems.append(
            "%d test(s) ctest resolved but did not report: %s. Nothing in the "
            "summary line would have said so." % (len(missing), missing[:20]))
    if extra:
        problems.append(
            "%d test(s) reported but not in the inventory: %s. The two "
            "artifacts are not from the same run." % (len(extra), extra[:20]))

    # Whitelist, not blacklist. Anything that is not exactly a pass is a
    # problem, including a status this reconciler has never heard of. Listing
    # the bad statuses instead would let a future ctest spelling through as
    # green, which is the failure mode this whole file exists to remove.
    bad = sorted(n for n, s in cases.items() if s not in ("pass", "skip"))
    if bad:
        problems.append("%d test(s) did not pass: %s"
                        % (len(bad), [(n, cases[n]) for n in bad[:20]]))

    # Deliberately overlapping with the line above. The suite-level errors
    # counter is optional for the pinned producer, so this walk is what stands
    # in for it, and a check that only exists as one clause of a catch-all is
    # one edit away from being gone without a test noticing.
    errored = sorted(n for n, s in cases.items() if s == "error")
    if errored:
        problems.append(
            "%d test(s) carry an <error> element: %s. The suite-level errors "
            "counter is optional for this producer, so this is where an "
            "errored case is seen." % (len(errored), errored[:20]))

    skipped = sorted(n for n, s in cases.items() if s == "skip")
    if skipped and not allow_skips:
        problems.append(
            "%d test(s) skipped: %s. In this suite a skip is an input the "
            "invocation did not bind." % (len(skipped), skipped))

    # The suite's own counters, independently of the per-case walk. A producer
    # that emitted run-looking cases while its suite header says failures="3"
    # is not a run this gate accepts, and neither is one whose header is absent.
    # Absent is judged against the pinned producer's profile: the counters it
    # emits must be there, and one it does not emit must be zero if it is.
    for key in REQUIRED_COUNTERS:
        values = counters.get(key) or []
        if not values or any(v is None for v in values):
            problems.append(
                "the JUnit report has a <testsuite> carrying no %s counter, so "
                "it cannot be checked against its own header." % key)

    # Every PRESENT value must be zero, in every suite. A key the pinned
    # producer omits is not a key that stops being checked wherever it does
    # appear, so the optional ones go through exactly this loop.
    for key in ("failures", "disabled") + OPTIONAL_COUNTERS:
        nonzero = [v for v in (counters.get(key) or []) if v not in (None, 0)]
        if nonzero:
            problems.append("a JUnit suite header reports %s=%s"
                            % (key, nonzero))
    if not allow_skips:
        nonzero = [v for v in (counters.get("skipped") or []) if v not in (None, 0)]
        if nonzero:
            problems.append("a JUnit suite header reports skipped=%s" % nonzero)

    totals = counters.get("tests") or []
    if totals and not any(v is None for v in totals) and sum(totals) != len(cases):
        problems.append(
            "the JUnit suite headers say tests=%d but %d <testcase> elements "
            "are present; the report disagrees with itself."
            % (sum(totals), len(cases)))

    for name in required:
        if name not in cases:
            problems.append(
                "required test %r is absent from the run. Set identity cannot "
                "see a test the build never registered." % name)
        elif cases[name] != "pass":  # a skip is not a pass here either
            problems.append("required test %r did not pass (%s)"
                            % (name, cases[name]))
    return problems


# A real report from the pinned producer, not a description of one. This is the
# shape ctest 3.22.1 actually writes: a bare <testsuite> root, its attributes
# one per tab-indented line, no errors attribute, classname/time on every case,
# a <system-out> child on cases that passed. Only the text inside <system-out>
# is shortened; every attribute, its presence, its absence and the layout are
# verbatim. It is the positive control for the whole producer profile: if this
# report does not reconcile, the gate cannot pass a run it judges correctly.
REAL_CTEST_3_22_REPORT = '''<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="(empty)"
	tests="2"
	failures="0"
	disabled="0"
	skipped="0"
	hostname=""
	time="0"
	timestamp="2026-09-05T23:22:07"
	>
	<testcase name="A" classname="A" time="0.0351552" status="run">
		<system-out>0 failures
</system-out>
	</testcase>
	<testcase name="B" classname="B" time="0.00689749" status="run">
		<system-out>0 failures
</system-out>
	</testcase>
</testsuite>
'''


def self_test():
    """Every check above, made to fail on purpose, driven through RAW XML.

    A reconciler with no negative test is indistinguishable from one that
    returns 0 unconditionally, and this one decides whether a gate is green.

    The cases go through junit_cases() on real files rather than through
    hand-built {name: status} dictionaries. A dictionary skips the parser, and
    the parser is where "a case with no status attribute" and "a suite header
    that disagrees with its own cases" are decided; a control that cannot see
    those is a control for the wrong function.
    """
    import os
    import tempfile

    checks = []

    def run(label, inventory, xml, required=(), allow_skips=False, want_fail=True,
            expect=None):
        """expect: a substring at least one problem must contain.

        Without it a negative control only says the reconciler went red, and a
        report that is malformed in some second way goes red too. Where the
        wording is the point, the case names the proposition it discriminates.
        """
        with tempfile.TemporaryDirectory() as tmp:
            jpath = os.path.join(tmp, "results.xml")
            with open(jpath, "w") as handle:
                handle.write(xml)
            ipath = os.path.join(tmp, "tests.json")
            with open(ipath, "w") as handle:
                json.dump({"tests": [{"name": n} for n in inventory]}, handle)
            try:
                names = inventory_names(ipath)
                cases, duplicates, counters = junit_cases(jpath)
                problems = reconcile(names, cases, duplicates, list(required),
                                     allow_skips, counters)
            except Exception as exc:                 # an unreadable report
                problems = ["unreadable: %s" % exc]
        ok = bool(problems) == want_fail
        if ok and expect is not None:
            ok = any(expect in p for p in problems)
        checks.append((label, ok, problems))

    def suite(cases_xml, tests="auto", failures=0, errors=None, disabled=0,
              skipped=0):
        """A <testsuite>; any counter passed as None is OMITTED, not zeroed.

        tests takes "auto" rather than None for that default, because None has
        to mean omitted here: the first version spelt the default None and the
        four missing-counter controls below silently emitted a tests attribute
        anyway, so the one for tests exercised nothing. Its own expectation
        caught that, which is the whole reason these run through raw XML.

        errors defaults to None because that is what the pinned producer does,
        so the ordinary cases below are written against a header shaped like a
        real one rather than an invented one that always carries every counter.
        """
        count = len(cases_xml) if tests == "auto" else tests
        attrs = [("tests", count), ("failures", failures), ("errors", errors),
                 ("disabled", disabled), ("skipped", skipped)]
        header = " ".join('%s="%s"' % (k, v) for k, v in attrs if v is not None)
        return "<testsuite %s>%s</testsuite>" % (header, "".join(cases_xml))

    def wrap(*suites_xml):
        """A <testsuites> wrapper, the shape a non-ctest producer emits."""
        return "<testsuites>%s</testsuites>" % "".join(suites_xml)

    ran = '<testcase name="%s" status="run"/>'
    good = [ran % "A", ran % "B"]

    run("a complete agreeing run passes", ["A", "B"], suite(good), want_fail=False)
    run("an empty inventory fails", [], suite([], tests=0))
    run("a test that did not report fails", ["A", "B", "C"], suite(good))
    run("a reported test not in the inventory fails", ["A"], suite(good, tests=2))
    run("a failure fails", ["A", "B"],
        suite([ran % "A", '<testcase name="B" status="run"><failure/></testcase>'],
              failures=1))
    run("a skip fails by default", ["A", "B"],
        suite([ran % "A", '<testcase name="B" status="run"><skipped/></testcase>'],
              skipped=1))
    run("a skip passes with --allow-skips", ["A", "B"],
        suite([ran % "A", '<testcase name="B" status="run"><skipped/></testcase>'],
              skipped=1), allow_skips=True, want_fail=False)
    run("a missing required test fails", ["A", "B"], suite(good), required=["C"])
    run("a duplicate name fails", ["A", "A"],
        suite([ran % "A", ran % "A"]))

    # The three the review named, and the reason they go through raw XML.
    run("a case with NO status attribute is not a pass", ["A", "B"],
        suite([ran % "A", '<testcase name="B"/>']))
    run("a case with an EMPTY status is not a pass", ["A", "B"],
        suite([ran % "A", '<testcase name="B" status=""/>']))
    run("run-looking cases do not override an adverse suite header", ["A", "B"],
        suite(good, failures=1))
    run("run-looking cases do not override a suite error counter", ["A", "B"],
        suite(good, errors=2))
    run("run-looking cases do not override a disabled counter", ["A", "B"],
        suite(good, disabled=1))
    run("a suite header that miscounts its own cases fails", ["A", "B"],
        suite(good, tests=99))
    run("a report with no counters at all fails",
        ["A", "B"], "<testsuite>%s</testsuite>" % "".join(good))
    run("an unparseable report fails", ["A", "B"], "<testsuite>not xml")

    # The producer profile: the positive control first, because a profile that
    # only ever rejects is a profile nothing proves is usable.
    run("the real ctest 3.22 report reconciles", ["A", "B"],
        REAL_CTEST_3_22_REPORT, want_fail=False)
    run("an absent errors counter is not itself a failure", ["A", "B"],
        suite(good, errors=None), want_fail=False)
    run("an errors counter that IS present must still be zero", ["A", "B"],
        suite(good, errors=1))
    run("an <error> case fails with no errors counter to see it", ["A", "B"],
        suite([ran % "A", '<testcase name="B" status="run"><error/></testcase>'],
              errors=None))
    for absent in ("tests", "failures", "disabled", "skipped"):
        run("a report missing its %s counter fails" % absent, ["A", "B"],
            suite(good, **{absent: None}))

    # Several suites in one report. The positive first, so that the negative
    # below cannot pass merely because the wrapper shape is rejected outright.
    run("two clean suites in a wrapper reconcile", ["A", "B"],
        wrap(suite([ran % "A"]), suite([ran % "B"])), want_fail=False)
    run("one suite omitting errors does not switch the check off for another",
        ["A", "B"],
        wrap(suite([ran % "A"], errors=None), suite([ran % "B"], errors=1)),
        expect="errors")
    run("a counter absent from only the SECOND suite still fails", ["A", "B"],
        wrap(suite([ran % "A"]), suite([ran % "B"], disabled=None)),
        expect="disabled")

    ok = True
    for label, passed, problems in checks:
        print("%-58s %s" % (label, "ok" if passed else "FAILED"))
        if not passed:
            ok = False
            print("   reconciler returned: %r" % (problems,))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--inventory", help="ctest --show-only=json-v1 output")
    ap.add_argument("--junit", help="ctest --output-junit output")
    ap.add_argument("--require", help="file of test names that must pass, one per line")
    ap.add_argument("--allow-skips", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        return self_test()
    if not args.inventory or not args.junit:
        ap.error("--inventory and --junit are required unless --self-test")

    required = []
    if args.require:
        with open(args.require) as handle:
            required = [line.strip() for line in handle
                        if line.strip() and not line.startswith("#")]

    try:
        inventory = inventory_names(args.inventory)
        cases, duplicates, counters = junit_cases(args.junit)
    except Exception as exc:                       # unreadable artifact
        print("FAIL: could not read the gate artifacts: %s" % exc, file=sys.stderr)
        return 1

    problems = reconcile(inventory, cases, duplicates, required, args.allow_skips,
                         counters)
    for problem in problems:
        print("FAIL: %s" % problem, file=sys.stderr)
    if problems:
        return 1
    print("reconciled: %d tests resolved, %d reported, all passed, %d required "
          "case(s) present" % (len(inventory), len(cases), len(required)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
