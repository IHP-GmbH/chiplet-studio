#!/usr/bin/env python3
"""Cross-reader check: this studio's C++ writer, read back by PyYAML.

Every test in this repository that asserts on the writer's output reads it with
yaml-cpp, which is the same YAML implementation the writer uses. Those tests can
only ever establish "this host reads back what this host wrote". The claim that
matters is that the OTHER reader in this ecosystem agrees, because a
`.chiplet` this studio saves is consumed by the KiCad plugin, by
chiplet-system-spec's reference reader and by interposer-pnr, all of which read
it with PyYAML. yaml-cpp handles scalars per YAML 1.2; PyYAML resolves per
YAML 1.1, where an unquoted 0755 is the integer 493 and an unquoted 2026-09-05
is a date.

The boundary measured here is the REAL producer's output, not a re-serialisation:
the fixture is loaded and saved through the C++ writer via the Python bindings,
and the bytes that lands on disk are what PyYAML is handed.

That covers the writer reached through load-and-save. It does NOT by itself cover
what a GDS import writes, which is a different producer with its own model
builder, so there is a second half: check_import_producer drives
chiplet_studio.write_single_gds_import, the same function src/ui/MainWindow.cpp
calls, and puts that one document through both readers. Coverage over a document
that resembles an import composes into an argument; this is the observation.

Its limit, stated so nobody takes the positive for more than it is: MainWindow
builds the spec from the GDS bounding box and the import dialog, and nothing here
sees that. What is witnessed is what the writer does with a spec.

Expected values are hand-authored in cross_reader_expectations.json. They are not
derived from the producer: a table generated from the writer would agree with the
writer by construction and could witness nothing.

Two claims come out of this check and they are kept apart on purpose. The first is
about quoting, and covers every string path the writer can reach. The second is
about number formatting, and covers only the four numeric paths a GDS import
actually emits: the writer hands yaml-cpp 2000.0, yaml-cpp writes 2000, and PyYAML
resolves an int where the writer had a double. Those four are pinned as what
happens today, not as what ought to happen; tests/CROSS_READER_CHECK.md says
what this leaves open.

Every missing dependency is a FAILURE, never a skip. A check that can vanish
without the gate noticing is not a check; the run would simply report a smaller
number that nobody reads.
"""
import json
import os
import pathlib
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
FIXTURES = HERE / "fixtures"

# bool is a subclass of int in Python, so an unguarded isinstance(node, int)
# would accept the YAML 1.1 resolution of `on` as a satisfied integer assertion.
TYPE_CHECKS = {
    "string": lambda node: isinstance(node, str),
    "int": lambda node: isinstance(node, int) and not isinstance(node, bool),
    "float": lambda node: isinstance(node, float),
}

TYPE_DIAGNOSIS = {
    "string": "The writer emitted it in a form YAML 1.1 resolves to another type.",
    "int": "The writer used to emit this as a bare integer; something changed how "
           "it formats numbers, and the two readers no longer agree on the type.",
    "float": "The writer used to emit this with a decimal point; something changed "
             "how it formats numbers, and the two readers no longer agree on the type.",
}


def die(msg):
    print("FAIL: " + msg, file=sys.stderr)
    raise SystemExit(1)


def load_deps():
    try:
        import yaml
    except ImportError as exc:
        die("PyYAML is not importable (%s). This check is not optional and does "
            "not skip: the image must provide the cross-reader venv. Rebuild "
            "with docker/Dockerfile.build." % exc)
    expectations = json.loads((FIXTURES / "cross_reader_expectations.json").read_text())
    want_version = expectations["pyyaml_version"]
    if yaml.__version__ != want_version:
        die("PyYAML %s is installed but this check pins %s. The pin is not "
            "cosmetic: it names the reader whose 1.1 resolution is being "
            "asserted. Update the pin and the expectations together, "
            "deliberately." % (yaml.__version__, want_version))
    try:
        import chiplet_studio
    except ImportError as exc:
        die("the chiplet_studio bindings are not importable (%s). The producer "
            "is half of this boundary; without it there is nothing to read." % exc)
    return yaml, chiplet_studio, expectations


def produce(chiplet_studio, out_path):
    """Real writer output: load the fixture and save it through the C++ writer."""
    src = FIXTURES / "cross_reader_emitted_paths.chiplet"
    if not src.exists():
        die("fixture missing: %s" % src)
    fmt = chiplet_studio.ChipletFormat()
    # Load and save are reported separately. They fail for different reasons and
    # a single message would blur them: a load failure means the fixture is not
    # a document this reader accepts, while a save failure most likely means the
    # save-time reader check refused the writer's own output, which is a finding
    # about the writer and not about cross-reader agreement at all.
    try:
        assembly = fmt.load(str(src))
    except Exception as exc:
        die("the producer cannot LOAD its own fixture (%s). This is a fixture "
            "problem, not a cross-reader disagreement: nothing was written, so "
            "no comparison happened." % exc)
    try:
        fmt.save(assembly, str(out_path))
    except Exception as exc:
        die("the producer REFUSED to save (%s). The save-time reader check "
            "rejects text this writer produced, so the two halves of this host "
            "disagree before PyYAML is even consulted. Look for %s.rejected."
            % (exc, out_path))
    if not os.path.exists(out_path) or os.path.getsize(out_path) == 0:
        die("the producer reported success but wrote nothing to %s" % out_path)


def assert_entry(doc, entry, where):
    """One expectation against one parsed document. Shared by both halves so the
    two cannot drift into asserting different things about the same shapes."""
    path, want = entry["path"], entry["value"]
    # No type key means string. That polarity is deliberate: a numeric path
    # added without an annotation is checked as a string and goes red, where
    # the opposite default would swallow the omission silently.
    declared = entry.get("type", "string")
    accepts = TYPE_CHECKS.get(declared)
    if accepts is None:
        return ["%s%s: expectation declares type %r, which this check has no "
                "rule for" % (where, path, declared)]
    found, node = resolve(doc, path)
    if not found:
        return ["%s%s: absent (%s)" % (where, path, node)]
    if not accepts(node):
        return ["%s%s: PyYAML typed it %s (%r), this expectation declares %s. %s"
                % (where, path, type(node).__name__, node, declared,
                   TYPE_DIAGNOSIS[declared])]
    if node != want:
        return ["%s%s: value %r, expected %r" % (where, path, node, want)]
    return []


def resolve(doc, path):
    """Address by index only. A selector matching on a field value would itself
    depend on that value surviving as a string, so a quoting regression would
    surface as 'path missing' rather than as the type error it is."""
    node = doc
    for part in path.split("."):
        name, _, rest = part.partition("[")
        if name:
            if not isinstance(node, dict) or name not in node:
                return (False, "no key %r" % name)
            node = node[name]
        while rest:
            index, _, rest = rest.partition("]")
            rest = rest.lstrip("[")
            try:
                i = int(index)
            except ValueError:
                return (False, "bad index %r" % index)
            if not isinstance(node, list) or i >= len(node):
                return (False, "index %d out of range" % i)
            node = node[i]
    return (True, node)


def check_import_producer(yaml, chiplet_studio, block, out_path):
    """One real GDS import, read by both readers.

    write_single_gds_import is the function src/ui/MainWindow.cpp calls, bound
    for this. Reaching the producer any other way, a checked-in golden or a
    document deposited by a C++ test, splits the claim across two independently
    schedulable ctest entries of which only chiplet_cross_reader is pinned in
    ci/gate_required_tests.txt.
    """
    failures = []
    spec = chiplet_studio.SingleGdsImportSpec()
    for field, value in block["spec"].items():
        if not hasattr(spec, field):
            failures.append("import: the spec has no field %r. The binding and "
                            "this fixture have drifted apart." % field)
            continue
        setattr(spec, field, value)
    if failures:
        return failures

    result = chiplet_studio.write_single_gds_import(spec, str(out_path))

    # The outcome is checked before the path, and that ordering is the point.
    # WrittenToFallback also carries a path, into a private mkdtemp, so reading
    # result.path alone would pass on a destination-write failure.
    if result.outcome != chiplet_studio.ImportWriteResult.Outcome.Written:
        return ["import: did not write to the destination it was given. "
                "outcome %s, path %r, message %r"
                % (result.outcome, result.path, result.message)]
    if result.path != str(out_path):
        return ["import: reported Written at %r rather than at %r"
                % (result.path, str(out_path))]

    try:
        doc = yaml.safe_load(pathlib.Path(out_path).read_text(encoding="utf-8"))
    except Exception as exc:
        return ["import: PyYAML cannot read what a GDS import wrote: %s" % exc]

    for entry in block["expect"]:
        failures.extend(assert_entry(doc, entry, "import "))

    # Same bytes, this studio's own reader. Not a round-trip check: the values
    # below were hand-authored, and asserting them through both readers is what
    # separates "the two readers agree" from "each reader is self-consistent".
    want = {e["path"]: e["value"] for e in block["expect"]}
    try:
        assembly = chiplet_studio.ChipletFormat().load(str(out_path))
    except Exception as exc:
        failures.append("import: this studio's own reader cannot read what its "
                        "import wrote: %s" % exc)
        return failures

    components = assembly.components()
    if len(components) != 1:
        failures.append("import: the document has %d components, expected 1"
                        % len(components))
        return failures

    # Addressed by index, like the corpus above: a lookup by id would depend on
    # the id surviving as a string, so a quoting regression would read as a
    # missing component rather than as the type error it is.
    component = components[0]
    dims = component.dimensions
    for path, got in [
            ("components[0].dimensions.width", dims.width),
            ("components[0].dimensions.height", dims.height),
            ("components[0].dimensions.thickness", dims.thickness),
            ("components[0].anchor", component.anchor),
            ("components[0].layout", component.layout_path),
            ("components[0].top_cell", component.top_cell),
            ("components[0].technology", component.technology)]:
        if got != want[path]:
            failures.append("import %s: this studio's reader got %r from the "
                            "same bytes PyYAML read as %r"
                            % (path, got, want[path]))

    technology = assembly.technology(block["spec"]["tech_id"])
    if technology is None:
        failures.append("import: no technology %r in the document"
                        % block["spec"]["tech_id"])
    elif technology.dbu != want["technologies.%s.dbu" % block["spec"]["tech_id"]]:
        failures.append("import technologies.%s.dbu: this studio's reader got "
                        "%r, expected %r"
                        % (block["spec"]["tech_id"], technology.dbu,
                           want["technologies.%s.dbu" % block["spec"]["tech_id"]]))

    # The disagreement itself, asserted rather than described. The writer holds
    # the dimensions as doubles and yaml-cpp hands them back as doubles, while
    # PyYAML hands its caller an int, because yaml-cpp writes 800.0 as 800.
    # Numerically equal, differently typed, and no single-reader test can say
    # so. Pinning it is recording what happens today, not endorsing it.
    for path, got in [("components[0].dimensions.width", dims.width),
                      ("components[0].dimensions.height", dims.height),
                      ("components[0].dimensions.thickness", dims.thickness)]:
        if not isinstance(got, float):
            failures.append("import %s: this studio's reader typed it %s, "
                            "expected a double. The int-vs-float split this "
                            "check pins is between the readers, not inside one."
                            % (path, type(got).__name__))
    return failures


def main():
    yaml, chiplet_studio, expectations = load_deps()

    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / "cross_reader_out.chiplet"
        produce(chiplet_studio, out)
        text = out.read_text(encoding="utf-8")
        try:
            doc = yaml.safe_load(text)
        except Exception as exc:
            die("PyYAML cannot read what this studio wrote: %s" % exc)

        failures = check_import_producer(
            yaml, chiplet_studio, expectations["import_producer"],
            pathlib.Path(tmp) / "cross_reader_import.chiplet")

    for entry in expectations["expect"]:
        failures.extend(assert_entry(doc, entry, ""))

    # Completeness, asserted against the vendored classification rather than
    # trusted. Three separate things are checked, because a classification that
    # is internally wrong can make a coverage claim look complete.
    classification = json.loads((FIXTURES / "cross_reader_classification.json").read_text())
    buckets = {k: v for k, v in classification.items()
               if not k.startswith("_") and isinstance(v, list)}

    # (a) the buckets partition the schema. A path in two buckets would be
    #     counted twice and could be reachable and out of scope at once.
    owner = {}
    for name in sorted(buckets):
        for path in buckets[name]:
            if path in owner:
                failures.append("%s is classified both %s and %s; the buckets are "
                                "meant to partition the schema" % (path, owner[path], name))
            owner[path] = name

    # (b) and they sum to the number of paths the pinned schema has. Moving a
    #     path between buckets leaves this alone, which is correct: that is a
    #     reclassification. Losing one lowers it.
    total = sum(len(v) for v in buckets.values())
    want_total = classification["addressable_total"]
    if total != want_total:
        failures.append(
            "the buckets hold %d paths but the classification pins %s at %d "
            "addressable. Either a path was dropped or the schema moved without "
            "the pin being updated; the schema_sha256_note field says how to "
            "tell those apart."
            % (total, classification["schema_source"], want_total))

    # (c) coverage is two-way. Everything reachable must have an expectation, and
    #     no expectation may name anything else. One direction alone lets a stale
    #     entry sit here forever, or a new reachable path go uncovered.
    reachable_strings = set(classification["emitted"]) | set(classification["canonicalized"])
    reachable_numbers = set(classification["numeric"])
    must_cover = reachable_strings | reachable_numbers
    covered = {e["schema_path"] for e in expectations["expect"]}
    for missing in sorted(must_cover - covered):
        failures.append("classified reachable but no expectation covers it: %s" % missing)
    for extra in sorted(covered - must_cover):
        failures.append(
            "an expectation names %s, which is classified %s rather than emitted, "
            "canonicalized or numeric. If the writer really emits it, the "
            "classification is stale; if not, this check is driving the model, "
            "which it must not." % (extra, owner.get(extra, "nothing at all")))

    if failures:
        print("cross-reader check FAILED (%d)" % len(failures), file=sys.stderr)
        for f in failures:
            print("  " + f, file=sys.stderr)
        return 1

    # Two claims, printed as two numbers. A combined figure would read as
    # progress across the numeric corpus, and there is no such progress: the
    # four numeric paths were chosen because an import emits them, not as a
    # sample of the 28.
    numeric_corpus = len(reachable_numbers) + len(buckets["non_string"])
    print("cross-reader check passed, PyYAML %s against the C++ writer's real output."
          % yaml.__version__)
    print("  strings: %d of %d reachable string paths (%d emitted + %d canonicalized)"
          % (len(covered & reachable_strings), len(reachable_strings),
             len(classification["emitted"]), len(classification["canonicalized"])))
    print("  numbers: %d of %d numeric paths, the ones a GDS import emits. The other "
          "%d are not covered here."
          % (len(covered & reachable_numbers), numeric_corpus,
             numeric_corpus - len(reachable_numbers)))
    print("  %d assertions, out of %d addressable schema paths. The two figures "
          "above are separate claims and do not add."
          % (len(expectations["expect"]), total))
    print("  producer: %d assertions over one real write_single_gds_import "
          "output, read by both. Outside the figures above on purpose; they are "
          "coverage of the schema, this is one document."
          % len(expectations["import_producer"]["expect"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
