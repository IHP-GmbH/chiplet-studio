#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 IHP GmbH
#
# run-gate.sh - the full ctest gate, with every input bound and a verdict that
# does not come from reading a log.
#
# WHY THE SUMMARY LINE IS NOT THE VERDICT. "100% tests passed, 0 tests failed
# out of N" is self-consistent for an empty suite, for a suite that registered
# fewer tests than it should have, and for one where the case that matters
# self-skipped: N is whatever ctest found. So the verdict here is a
# reconciliation between two independent artifacts, `ctest --show-only=json-v1`
# (what was resolved from the build) and `ctest --output-junit` (what ran and
# with what status), plus a list of cases that must be present and pass. See
# ci/reconcile_gate.py.
#
# WHY EVERY INPUT IS BOUND. Several tests decline to run when an input is
# missing rather than fail, so a partial invocation prints a green summary while
# not executing them. This project reported ten "known display-only skips" for
# months; nine were nothing of the kind, and two of those were the tests that
# certify save safety.
#
# Usage:
#   ./scripts/run-gate.sh [--allow-skips] [--log-dir DIR]
#
# Environment:
#   CHIPLET_ADK_ROOT   directory holding interposer/, interconnect_pdk/ and
#                      gds_to_kicad/ (default: the parent of this repository)
#   WIREBOND_DEMO_CHIPLET_HOST
#                      host path to the wire-bond demo .chiplet the coordinate
#                      frame contract tests read
#   CHIPLET_GATE_DEPS_HOST
#                      host directory to bind at the container prefix the
#                      FetchContent source directories in build/CMakeCache.txt
#                      resolve to, when that prefix is not under /workspace. A
#                      build configured offline records paths like
#                      /deps/googletest-src, and nothing else mounts them.
#   CHIPLET_GATE_IMAGE docker image, REQUIRED: the adk-tools studio-builder
#                      stage, so the gate builds in the environment the
#                      product image is built in. It carries its own KLayout,
#                      which this run links and loads: build/ must be configured
#                      with KLAYOUT_BUILD_DIR at that image path (see
#                      scripts/configure-gate.sh).
#   CROSS_READER_PYTHON
#                      interpreter with PyYAML for the cross-reader check,
#                      passed into the container when set (the image's tools
#                      venv). Its PyYAML version is checked in the run.
#   CHIPLET_GATE_CPUS  cpu limit (default: 6)
#   CHIPLET_GATE_JOBS  make -j value (default: the cpu limit). Separate from the
#                      cpu limit because they answer different questions: the
#                      cap is how much of the host the run may use, the job
#                      count is how many compilers share the memory cap. Four
#                      concurrent g++ inside 8 GiB is the shape that gets a run
#                      OOM-killed halfway through a build.
#   CHIPLET_GATE_MEM   memory limit (default: 12g)
#   CHIPLET_GATE_TIMEOUT   seconds before the CLIENT is aborted (default: 1800)
#   CHIPLET_GATE_INNER_TIMEOUT
#                      seconds before ctest is aborted INSIDE the container
#                      (default: 1500; must be less than the outer one)
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT="${CHIPLET_ADK_ROOT:-$(dirname "$REPO")}"
IMAGE="${CHIPLET_GATE_IMAGE:-}"
# The PyYAML the cross-reader expectations were established against. A
# different version can read the same bytes differently, and the check would
# then be comparing the writer with something else.
PYYAML_PIN="6.0.3"
CPUS="${CHIPLET_GATE_CPUS:-6}"
# ${VAR-default}, NOT ${VAR:-default}. With the colon an explicitly empty value
# is indistinguishable from unset and silently becomes the cpu count, and an
# empty value is almost always a scripting mistake (CHIPLET_GATE_JOBS=$SOMETHING
# where SOMETHING was never set) rather than a request for the default. Without
# it, unset means the default and empty reaches the check below and aborts.
JOBS="${CHIPLET_GATE_JOBS-$CPUS}"
MEM="${CHIPLET_GATE_MEM:-12g}"
RUN_TIMEOUT="${CHIPLET_GATE_TIMEOUT:-1800}"
INNER_TIMEOUT="${CHIPLET_GATE_INNER_TIMEOUT:-1500}"
ALLOW_SKIPS=0
LOGDIR=""

while [ $# -gt 0 ]; do
  case "$1" in
    --allow-skips) ALLOW_SKIPS=1; shift;;
    --log-dir) LOGDIR="$2"; shift 2;;
    -h|--help) sed -n '5,/^set -u$/p' "${BASH_SOURCE[0]}" | sed '$d'; exit 0;;
    *) echo "unknown option: $1" >&2; exit 2;;
  esac
done
# Validated as a positive integer before it reaches make, because the failure
# is quiet otherwise: `make -j` with an empty value is UNBOUNDED parallelism,
# which on a 32-core host inside an 8 GiB cap is a memory bomb, and `-j0` is a
# usage error make reports in its own words halfway into a captured run.
# No default image. The one this used to default to was a separate build
# environment that had to be kept alive beside the product's, and it was gone
# from the host when a gate was granted: a default names an image that may not
# be the one anybody meant.
[ -n "$IMAGE" ] || {
  echo "ABORT: CHIPLET_GATE_IMAGE is unset. Name the adk-tools studio-builder" >&2
  echo "       image this gate builds and runs in." >&2; exit 2; }
case "$JOBS" in
  ''|*[!0-9]*)
    echo "ABORT: CHIPLET_GATE_JOBS must be a positive integer, got '$JOBS'" >&2
    exit 2;;
esac
[ "$JOBS" -ge 1 ] || {
  echo "ABORT: CHIPLET_GATE_JOBS must be at least 1, got '$JOBS'" >&2; exit 2; }

[ "$INNER_TIMEOUT" -lt "$RUN_TIMEOUT" ] || {
  echo "ABORT: the in-container timeout ($INNER_TIMEOUT s) must be less than the"
  echo "       client timeout ($RUN_TIMEOUT s), or the client dies first and the"
  echo "       container's own abort never runs."; exit 2; }

cd "$REPO" || exit 1
SHA="$(git rev-parse HEAD 2>/dev/null || echo unknown)"
LOGDIR="${LOGDIR:-$REPO/build/gate-logs/$SHA}"

# A capture is written once, into a directory that must not already exist.
# Checking for gate.log or verdict.txt was too narrow: a directory holding a
# partial capture, or anything else, would have been written into and the two
# runs' artifacts would sit under one name with nothing saying so.
if [ -e "$LOGDIR" ]; then
  echo "ABORT: $LOGDIR already exists. A capture directory is created by this"
  echo "       script and written once. Move it aside or pass --log-dir."; exit 2
fi
mkdir -p "$LOGDIR" || exit 1
LOG="$LOGDIR/gate.log"

# The capture stamps its own times. It did not, and every time anyone reported
# about a run therefore came from outside it: an untracked invocation script, or
# filesystem mtimes whose zone the reader had to supply. That gap is how a
# start time once got read off an `ls` listing in local time and reported with
# a Z on the end. `date -u` here, written into the capture, so the artifact
# answers the question itself and the zone is not a matter of interpretation.
GATE_START_ISO="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
GATE_START_EPOCH="$(date -u +%s)"
STAMPS="$LOGDIR/stamps.txt"
# Written as each moment happens, not assembled at the end. Held in variables
# until a final write, a run that refuses early or is interrupted leaves a
# capture with no clock at all, which is the state the previous version would
# have been in for every abort between here and the verdict. An end is only
# ever written when it is reached: an interrupted run has no end, and inventing
# one would put a number in the capture that describes nothing.
#
# The provenance line is part of the record rather than a convention, because
# the failure this closes was a time reported in the wrong zone: this host runs
# two hours ahead of UTC, and a reader who has to supply the zone eventually
# supplies the wrong one.
{ echo "# times are UTC, from date -u called inside run-gate.sh on the host"
  echo "# host local offset when this capture opened: $(date +%z)"
} > "$STAMPS" || { echo "ABORT: cannot open the capture's stamps file"; exit 2; }
# Every stamp is fail-closed. A capture that cannot record when it ran must not
# be able to reach a verdict; before this, each of these writes could fail and
# the run still finish GREEN.
stamp() {  # stamp <key> <value>
  printf '%-11s %s\n' "$1" "$2" >> "$STAMPS" || {
    echo "ABORT: cannot append $1 to $STAMPS"; exit 2; }
}
stamp gate_start "$GATE_START_ISO"

# The runner records its own bytes first, so the capture can be read without
# trusting a later copy of this file.
cp "${BASH_SOURCE[0]}" "$LOGDIR/runner-as-executed.sh" || exit 2
( cd "$LOGDIR" && sha256sum runner-as-executed.sh > runner-as-executed.sh.sha256 ) || exit 2
cp ci/reconcile_gate.py ci/gate_required_tests.txt "$LOGDIR/" || exit 2
( cd "$LOGDIR" && sha256sum reconcile_gate.py gate_required_tests.txt \
    >> runner-as-executed.sh.sha256 ) || exit 2
cat "$LOGDIR/runner-as-executed.sh.sha256"

# --- the build must be configured to THIS source, with Python on -------------
# Python OFF silently drops the binding tests and the cross-reader check, and a
# build configured against a different source directory tests something else.
CACHE="$REPO/build/CMakeCache.txt"
[ -f "$CACHE" ] || { echo "ABORT: no build/ configured. Run ./scripts/build-docker.sh first."; exit 2; }
CACHE_SRC="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$CACHE")"
[ "$CACHE_SRC" = "/workspace" ] || {
  echo "ABORT: build/ is configured for source dir '$CACHE_SRC', not the"
  echo "       /workspace this container mounts the repository at."; exit 2; }
grep -q '^ENABLE_PYTHON_SCRIPTING:BOOL=ON' "$CACHE" || {
  echo "ABORT: build/ is configured with ENABLE_PYTHON_SCRIPTING OFF, which"
  echo "       drops the binding tests and the cross-reader check without"
  echo "       failing anything."; exit 2; }
echo "build configured for $CACHE_SRC with Python ON"

# --- KLayout: the image's own, the one the build links ------------------------
# Read from the cache, not named here, so the libraries the suite loads are by
# construction the ones the build linked against. It must lie outside
# /workspace: a KLayout under the mounted tree is a prebuilt copy restored by
# hand on one host, which is what this gate used to load and what left it
# unable to run anywhere else.
KLAYOUT_BIN="$(sed -n 's/^KLAYOUT_BUILD_DIR:PATH=//p' "$CACHE")"
case "$KLAYOUT_BIN" in
  /workspace|/workspace/*|'')
    echo "ABORT: build/ is configured with KLAYOUT_BUILD_DIR='$KLAYOUT_BIN'. The"
    echo "       gate links and loads the image's own KLayout; configure with"
    echo "       scripts/configure-gate.sh."; exit 2;;
  /*) ;;
  *) echo "ABORT: KLAYOUT_BUILD_DIR '$KLAYOUT_BIN' is not an absolute path."; exit 2;;
esac
# The headers come from this tree's extern/klayout and the libraries from the
# image's. The run compares the two trees' headers under every include
# directory below, so a submodule moved without the image is a red run and not
# a binary built against one KLayout and loaded with another.
KLAYOUT_IMAGE_SRC="$(dirname "$KLAYOUT_BIN")/src"
echo "KLayout from the image: $KLAYOUT_BIN (headers compared with $KLAYOUT_IMAGE_SRC)"

# --- where the dependency sources actually live ------------------------------
# FetchContent records, per package, the directory it resolved that package's
# source to, and that directory is what the compiler reads on every translation
# unit that includes it. It is not always inside the repository. Configured in
# tree it is build/_deps, which .gitignore excludes; configured offline with
# FETCHCONTENT_SOURCE_DIR_* it is a path like /deps/googletest-src that exists
# only because something binds it into the container. Both were outside every
# inventory this gate had, and the second was outside the invocation as well:
# nothing here mounted it, so the run would have failed the build inside the
# capture with the cause several levels down in a compiler error.
#
# The packages are not listed here. Every *_SOURCE_DIR the cache records is
# read, the project's own is dropped, and a directory inside another is dropped
# as covered by its parent. Adding a dependency therefore cannot leave one
# unhashed by being forgotten in a list, which is the failure this whole block
# exists to prevent and the one a hand-written list reintroduces.
DEPS_HOST_IN="${CHIPLET_GATE_DEPS_HOST:-}"
DEP_DIRS=()
while IFS= read -r v; do
  [ -n "$v" ] || continue
  [ "$v" = "$CACHE_SRC" ] && continue
  keep=1
  for k in "${DEP_DIRS[@]}"; do
    case "$v" in "$k"/*) keep=0; break;; esac
  done
  [ "$keep" -eq 1 ] && DEP_DIRS+=("$v")
done < <(sed -n 's/^[A-Za-z0-9_-]*_SOURCE_DIR:STATIC=//p' "$CACHE" | LC_ALL=C sort -u)
[ "${#DEP_DIRS[@]}" -gt 0 ] || {
  echo "ABORT: build/CMakeCache.txt records no dependency source directory."
  echo "       This build fetches googletest, nlohmann_json and pybind11, so an"
  echo "       empty set means the cache was not read as expected, not that"
  echo "       there is nothing to cover."; exit 2; }

DEPS_PREFIX=""
DEP_SRC_HOST=()
{ echo "# cache $CACHE"
  echo "# container-source-dir -> host-path (via)"
} > "$LOGDIR/dependency-mapping.txt"
for v in "${DEP_DIRS[@]}"; do
  case "$v" in
    /workspace)   dep_host="$REPO";                     dep_via="workspace-mount";;
    /workspace/*) dep_host="$REPO/${v#/workspace/}";    dep_via="workspace-mount";;
    /*)
      dep_prefix="/$(printf '%s' "${v#/}" | cut -d/ -f1)"
      if [ -z "$DEPS_PREFIX" ]; then
        DEPS_PREFIX="$dep_prefix"
      elif [ "$DEPS_PREFIX" != "$dep_prefix" ]; then
        echo "ABORT: the dependency sources resolve under more than one container"
        echo "       prefix ($DEPS_PREFIX and $dep_prefix). This runner binds one"
        echo "       host directory, and guessing a second would be inventing a"
        echo "       mount nobody asked for."; exit 2
      fi
      if [ -z "$DEPS_HOST_IN" ]; then
        echo "ABORT: the cache resolves a dependency source to"
        echo "         $v"
        echo "       which is outside /workspace, and CHIPLET_GATE_DEPS_HOST is unset."
        echo "       Set it to the host directory to bind at $dep_prefix. Without it the"
        echo "       container has no such path, the build fails, and the reason"
        echo "       reads as a missing header rather than a missing mount."; exit 2
      fi
      dep_host="$DEPS_HOST_IN/${v#"$dep_prefix"/}"
      dep_via="deps-bind"
      ;;
    *) echo "ABORT: dependency source dir '$v' is not an absolute path."; exit 2;;
  esac
  # The check that makes the mapping worth writing down. A bind whose layout
  # does not match what the cache resolved is the specific way this goes wrong
  # quietly: a directory holding googletest/ and nlohmann_json/ satisfies a
  # human reading "the dependency sources" and satisfies nothing the compiler
  # asked for, which was googletest-src/ and nlohmann_json-src/.
  [ -d "$dep_host" ] || {
    echo "ABORT: the cache resolves a dependency source to"
    echo "         $v"
    echo "       which this run would have to read on the host at"
    echo "         $dep_host"
    echo "       and that is not a directory. The bind does not match what the"
    echo "       build was configured against."; exit 2; }
  DEP_SRC_HOST+=("$dep_host")
  echo "$v -> $dep_host ($dep_via)" >> "$LOGDIR/dependency-mapping.txt"
done
# Recorded whether set or empty: an empty FETCHCONTENT_SOURCE_DIR_* is how an
# in-tree configure looks, and reading the capture later should not require
# knowing that.
sed -n 's/^\(FETCHCONTENT_SOURCE_DIR_[A-Z0-9_]*\):PATH=/override \1=/p' "$CACHE" \
  >> "$LOGDIR/dependency-mapping.txt"
if [ -n "$DEPS_PREFIX" ]; then
  echo "bind $DEPS_HOST_IN -> $DEPS_PREFIX (ro)" >> "$LOGDIR/dependency-mapping.txt"
else
  echo "bind (none needed: every dependency source is under /workspace)" \
    >> "$LOGDIR/dependency-mapping.txt"
fi
cat "$LOGDIR/dependency-mapping.txt"

# --- source inventory, BEFORE ------------------------------------------------
# Dirty source is rejected outright: a capture that names a commit while the
# tree carried something else describes a build nobody can reproduce.
DIRTY="$(git status --porcelain)"
if [ -n "$DIRTY" ]; then
  echo "ABORT: the source tree is dirty. The capture would name $SHA and test"
  echo "       something else:"; echo "$DIRTY"; exit 2
fi
# Byte hashes, not just git identifiers. A commit id and a clean status say the
# tree matches what git recorded; they say nothing about the inputs git does not
# track, and the dependency sources and submodule contents walked below are
# exactly that. The KLayout libraries the run loads are the image's and are
# hashed inside the container, before the build and after the tests.
# Every stage is checked on its own, and that is the whole point of the shape.
# The first version of this function ran both stages inside one redirected
# group, and a group's exit status is its LAST command's: a `git ls-files` that
# failed, or a tracked file that became unreadable part-way through the walk,
# was overwritten by the KLayout stage succeeding, and the run carried on with a
# manifest quietly missing whatever it could not read. Returning from inside the
# walk did not help either, since a `while` on the right of a pipe runs in a
# subshell and its `return` never reaches the function.
source_manifest() {  # source_manifest <output path>
  local out="$1"
  local list="$1.tracked-list"
  if ! ( set -o pipefail
         git ls-files --stage \
           | awk '$1 != "160000" { $1=""; $2=""; $3=""; sub(/^   /,""); print }'
       ) > "$list"; then
    echo "ABORT: cannot list the tracked sources" >&2; return 1
  fi
  if [ ! -s "$list" ]; then
    echo "ABORT: the tracked source list is empty" >&2; return 1
  fi
  : > "$out" || return 1
  while IFS= read -r f; do
    if ! sha256sum -- "$f" >> "$out"; then
      echo "ABORT: cannot hash the tracked source $f" >&2; return 1
    fi
  done < "$list"
  # The dependency sources FetchContent resolved, at the host paths the mapping
  # recorded. The compiler reads these on every translation unit that includes
  # them and nothing else here covers them: in tree they sit under build/, which
  # .gitignore excludes, and out of tree they are not in the repository at all.
  # .git is pruned for the reason it is pruned under the PDK trees, and because
  # in these checkouts it is most of the bytes: nlohmann_json-src carries a
  # 245 MiB pack the build never reads.
  local d
  for d in "${DEP_SRC_HOST[@]}"; do
    if ! ( set -o pipefail
           find "$d" -name .git -prune -o -type l -printf 'symlink %p -> %l\n' \
             | LC_ALL=C sort
         ) >> "$out"; then
      echo "ABORT: cannot inventory the symlinks under the dependency source $d" >&2
      return 1
    fi
    if ! ( set -o pipefail
           find "$d" -name .git -prune -o -type f -print0 \
             | LC_ALL=C sort -z | xargs -0 -r sha256sum
         ) >> "$out"; then
      echo "ABORT: cannot hash the dependency source $d" >&2; return 1
    fi
  done
  # GDS3D, which is compiled into this binary from source, and the KLayout
  # headers on the include path. Both live in submodules, and the tracked-file
  # walk above drops mode 160000 entries on purpose: a gitlink records a commit
  # id, not the bytes in the working tree, so every file inside a submodule was
  # skipped. The superproject's `git status` does report ' M extern/GDS3D' when
  # submodule content changes, so drift was detected; it was never recorded, and
  # a detection naming a directory is not a manifest.
  if ! ( set -o pipefail
         find extern/GDS3D -name .git -prune -o -type f -print0 \
           | LC_ALL=C sort -z | xargs -0 -r sha256sum
       ) >> "$out"; then
    echo "ABORT: cannot hash the GDS3D sources" >&2; return 1
  fi
  # Read out of CMakeLists.txt rather than listed here, so adding an include
  # directory cannot quietly leave its headers unhashed.
  #
  # Depth 1 is a BOUND, not a completeness argument. An earlier version of this
  # comment said it was "what an -I directory exposes to #include", which is
  # false: one -I exposes everything beneath it, so #include <db/dbLayout.h>
  # reaches a nested path from a single include directory. What makes the bound
  # sufficient is a measurement, not a rule. The 203 retained .o.d files from
  # the qualified build of this tree name 158 distinct consumed KLayout files,
  # and all 158 are covered by this walk. That is evidence about THIS
  # configuration and nothing wider: a source that reached a nested KLayout
  # header would need this widened, and nothing here would say so.
  #
  # Same read-from-a-file shape as the tracked walk above and for the same
  # reason: a `while` on the right of a pipe runs in a subshell and its
  # `return` never reaches this function.
  local klist="$out.klayout-include-dirs"
  if ! ( set -o pipefail
         sed -n '/^include_directories(/,/^)/p' CMakeLists.txt \
           | sed -n 's|^[[:space:]]*\${KLAYOUT_SRC_DIR}\(/[A-Za-z0-9_/]*\)\?[[:space:]]*$|extern/klayout/src\1|p'
       ) > "$klist"; then
    echo "ABORT: cannot read the KLayout include directories from CMakeLists.txt" >&2
    return 1
  fi
  if [ ! -s "$klist" ]; then
    echo "ABORT: CMakeLists.txt declares no KLayout include directory. The block" >&2
    echo "       moved or its shape changed, and this walk now covers nothing" >&2
    echo "       while still succeeding." >&2
    return 1
  fi
  while IFS= read -r d; do
    if [ ! -d "$d" ]; then
      echo "ABORT: CMakeLists.txt puts $d on the include path and it does not exist" >&2
      return 1
    fi
    if ! ( set -o pipefail
           find "$d" -maxdepth 1 -type f \( -name '*.h' -o -name '*.hpp' \) -print0 \
             | LC_ALL=C sort -z | xargs -0 -r sha256sum
         ) >> "$out"; then
      echo "ABORT: cannot hash the KLayout headers under $d" >&2; return 1
    fi
  done < "$klist"
  rm -f "$klist"
  rm -f "$list"
}
{ echo "HEAD $SHA"
  echo "tree $(git rev-parse HEAD^{tree})"
  echo "branch $(git rev-parse --abbrev-ref HEAD)"
  echo "dirty (none)"
} > "$LOGDIR/source-inventory-before.txt"
source_manifest "$LOGDIR/source-manifest-before.txt" || {
  echo "ABORT: could not hash the tracked source and dependency inputs"; exit 2; }
# An empty or truncated manifest would compare equal to an equally empty one at
# the end and the comparison would pass by having nothing to say. Two things it
# must contain: this script, and at least one KLayout library, since those are
# the untracked inputs the manifest exists for.
grep -q " scripts/run-gate.sh$" "$LOGDIR/source-manifest-before.txt" || {
  echo "ABORT: the source manifest does not list this script; it is empty or"
  echo "       truncated, and comparing it later would prove nothing."; exit 2; }
# One probe per class the walk added, for the reason the two above exist: a
# stage that silently matched nothing yields a manifest that compares equal to
# an equally empty one at the end, and the comparison passes by having nothing
# to say.
grep -q " extern/GDS3D/" "$LOGDIR/source-manifest-before.txt" || {
  echo "ABORT: the source manifest lists no GDS3D source, and eighteen of its"
  echo "       .cpp are compiled into this binary."; exit 2; }
grep -q " extern/klayout/src/db/db/" "$LOGDIR/source-manifest-before.txt" || {
  echo "ABORT: the source manifest lists no KLayout header from db/db, which"
  echo "       CMakeLists.txt puts on the include path."; exit 2; }
for probe in "${DEP_SRC_HOST[@]}"; do
  grep -q " $probe/" "$LOGDIR/source-manifest-before.txt" || {
    echo "ABORT: the source manifest lists nothing under the dependency source"
    echo "       $probe, which the cache says this build compiles against."
    exit 2; }
done
echo "source manifest: $(wc -l < "$LOGDIR/source-manifest-before.txt") files, \
sha256 $(sha256sum "$LOGDIR/source-manifest-before.txt" | cut -c1-16)"
cat "$LOGDIR/source-inventory-before.txt"

# --- inputs ------------------------------------------------------------------
MISSING=0
need() {  # need <path> <what its absence silences>
  [ -e "$1" ] && return 0
  echo "MISSING: $1"; echo "         silences: $2"; MISSING=$((MISSING + 1))
}
FIXTURE="${WIREBOND_DEMO_CHIPLET_HOST:-}"
if [ -z "$FIXTURE" ] || [ ! -f "$FIXTURE" ]; then
  echo "MISSING: WIREBOND_DEMO_CHIPLET_HOST (${FIXTURE:-unset})"
  echo "         silences: CoordFrameContractWirebondDemo (6), RenderModeTest.WirebondDemoLoadsWithTransparentDefaults"
  MISSING=$((MISSING + 1))
fi
need "$ROOT/interposer" "interposer stackup resolution"
need "$ROOT/interconnect_pdk" "CuPillar and bump geometry"
# Named individually, not as a parent directory: these are the two assets under
# the gds_to_kicad root that the suite actually reads, and an unresolved root
# MASKS the next one, so binding a directory that happens to exist while an
# asset inside it does not is how this took four runs to diagnose.
need "$ROOT/gds_to_kicad/pdks/sg13g2.lyp" "sg13g2 layer colours"
need "$ROOT/gds_to_kicad/gds_files/for_thermal_eval/Metal_Test/Metal_Test.gds" "the real-GDS load path"
if [ "$MISSING" -gt 0 ]; then
  if [ "$ALLOW_SKIPS" -eq 0 ]; then
    echo "ABORT: $MISSING input(s) missing. Set CHIPLET_ADK_ROOT and"
    echo "       WIREBOND_DEMO_CHIPLET_HOST, or pass --allow-skips and accept a"
    echo "       green that did not execute those tests."; exit 2
  fi
  echo "WARNING: running with $MISSING input(s) missing because --allow-skips was given."
fi

IMAGE_ID="$(docker image inspect "$IMAGE" --format '{{.Id}}' 2>/dev/null)"
[ -n "$IMAGE_ID" ] || { echo "ABORT: no docker image $IMAGE"; exit 2; }
# Hashing an input is fail-closed, like hashing a product. The checks above
# test existence, not readability, so a present-but-unreadable mount would have
# made sha256sum fail, the line would simply be absent from the freeze, and the
# run would have carried on with a capture that under-records its own inputs.
# Aborts the run, it does not return a code: `exit` inside the redirected group
# below ends this script directly, so a caller that tried to branch on a return
# value would be writing a path nothing takes.
hash_input() {  # hash_input <label> <path>
  local line
  if ! line="$(sha256sum "$2" 2>&1)"; then
    echo "ABORT: cannot hash the $1 input ($2): $line" >&2
    echo "       An input that cannot be read cannot be recorded, and a capture" >&2
    echo "       that under-records its own inputs is not evidence." >&2
    exit 2
  fi
  echo "$1 $line"
}
{ echo "image $IMAGE $IMAGE_ID"
  echo "klayout_bin $KLAYOUT_BIN (in the image)"
  echo "pyyaml_pin $PYYAML_PIN"
  echo "cross_reader_python ${CROSS_READER_PYTHON:-(unset, so the check script default)}"
  if [ -n "$FIXTURE" ] && [ -f "$FIXTURE" ]; then hash_input fixture "$FIXTURE"; fi
  for asset in "$ROOT/gds_to_kicad/pdks/sg13g2.lyp" \
               "$ROOT/gds_to_kicad/gds_files/for_thermal_eval/Metal_Test/Metal_Test.gds"; do
    if [ -f "$asset" ]; then hash_input asset "$asset"; fi
  done
  for sib in interposer interconnect_pdk; do
    [ -d "$ROOT/$sib" ] && echo "$sib $(git -C "$ROOT/$sib" rev-parse HEAD 2>/dev/null) clean=$([ -z "$(git -C "$ROOT/$sib" status --porcelain 2>/dev/null)" ] && echo yes || echo no)"
  done
  echo "allow_skips $ALLOW_SKIPS"
} > "$LOGDIR/freeze.txt"
cat "$LOGDIR/freeze.txt"

# The freeze above records the external inputs only as they were BEFORE, and a
# commit id with a clean status does not close that: it says nothing about the
# files git ignores, and these trees carry generated artefacts the suite reads.
# The mounts are read-only, but read-only is a property of the mount and not of
# the host; nothing stops another process on this shared machine from rewriting
# a PDK tree while the suite reads it. So the external inputs get the same
# before/after byte treatment the source already had.
#
# .git is excluded on purpose: this script runs `git status` on those trees, and
# that rewrites their index, so including it would make every run differ from
# itself and the comparison would be noise. Symlinks are recorded as their
# targets rather than followed, since a link repointed at a byte-identical file
# is still a changed input.
external_manifest() {  # external_manifest <output path>
  local out="$1" f d
  : > "$out" || return 1
  for f in "$FIXTURE" \
           "$ROOT/gds_to_kicad/pdks/sg13g2.lyp" \
           "$ROOT/gds_to_kicad/gds_files/for_thermal_eval/Metal_Test/Metal_Test.gds"; do
    [ -n "$f" ] && [ -e "$f" ] || continue
    if ! sha256sum -- "$f" >> "$out"; then
      echo "ABORT: cannot hash the external input $f" >&2; return 1
    fi
  done
  for d in "$ROOT/interposer" "$ROOT/interconnect_pdk"; do
    [ -d "$d" ] || continue
    if ! ( set -o pipefail
           find "$d" -name .git -prune -o -type l -printf 'symlink %p -> %l\n' \
             | LC_ALL=C sort
         ) >> "$out"; then
      echo "ABORT: cannot inventory the symlinks under $d" >&2; return 1
    fi
    if ! ( set -o pipefail
           find "$d" -name .git -prune -o -type f -print0 \
             | LC_ALL=C sort -z | xargs -0 -r sha256sum
         ) >> "$out"; then
      echo "ABORT: cannot hash the files under $d" >&2; return 1
    fi
  done
}
external_manifest "$LOGDIR/external-manifest-before.txt" || {
  echo "ABORT: could not inventory the external inputs"; exit 2; }
# Same reason the source manifest is probed: an empty inventory compares equal
# to an equally empty one at the end and passes by having nothing to say.
for probe in interposer interconnect_pdk; do
  [ -d "$ROOT/$probe" ] || continue
  grep -q "$ROOT/$probe/" "$LOGDIR/external-manifest-before.txt" || {
    echo "ABORT: the external inventory lists nothing under $probe, so"
    echo "       comparing it after the run would prove nothing."; exit 2; }
done
echo "external inventory: $(wc -l < "$LOGDIR/external-manifest-before.txt") entries, \
sha256 $(sha256sum "$LOGDIR/external-manifest-before.txt" | cut -c1-16)"

# The include directories, for the header comparison inside the container. Same
# read as the manifest's, kept in the capture because the run reads it.
if ! ( set -o pipefail
       sed -n '/^include_directories(/,/^)/p' CMakeLists.txt \
         | sed -n 's|^[[:space:]]*\${KLAYOUT_SRC_DIR}\(/[A-Za-z0-9_/]*\)\?[[:space:]]*$|extern/klayout/src\1|p'
     ) > "$LOGDIR/klayout-include-dirs.txt" || [ ! -s "$LOGDIR/klayout-include-dirs.txt" ]; then
  echo "ABORT: cannot read the KLayout include directories for the header comparison"
  exit 2
fi

# --- the run -----------------------------------------------------------------
# Named so the container can be reached after the client is gone. A stopped
# client does NOT stop the container: dockerd keeps it running, detached.
CONTAINER="chiplet-gate-$$-${SHA:0:12}"
trap 'docker kill "$CONTAINER" >/dev/null 2>&1; exit 130' INT TERM

# The container's whole workload is written to a file in the capture and run
# under ONE timeout. Wrapping only ctest left `make` unbounded, so a hung build
# could sit there until the client timeout fired, at which point the client dies
# and the detached workload keeps going.
cat > "$LOGDIR/inner-work.sh" <<'WORK' || exit 2
#!/bin/bash
set -u
# A private runtime dir, mode 700. Without one Qt warns and falls back to a
# shared location, which is both noisy and shared.
export XDG_RUNTIME_DIR="/tmp/xdg-gate-$$"
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR"
cd /workspace/build || exit 90

products() { sha256sum tests/chiplet_tests chiplet-studio \
                       python/chiplet_studio.cpython-*-x86_64-linux-gnu.so; }

# What this line can honestly say is whether the three final products were all
# present, and nothing more. An earlier version called the two cases fresh and
# incremental, which overclaims in the direction that matters: `products` fails
# if ANY one of the three is missing, and a missing binary says nothing about
# whether the object tree behind it is empty. A run over a full object tree with
# one deleted binary would have been labelled a fresh build.
if products > /logs/product-inventory-before-build.txt 2>/dev/null; then
    echo "PRODUCTS_BEFORE=complete"
else
    : > /logs/product-inventory-before-build.txt
    echo "PRODUCTS_BEFORE=incomplete"
fi

# What the image sets is an input like any mounted tree: it carries PDK roots
# and tool paths of its own, and a test that reads one would otherwise depend on
# something the capture never named.
env | LC_ALL=C sort > /logs/container-env.txt

# The image's KLayout, every file under the directory the build links and the
# suite loads. Not only *.so: this tree is the image's and not a hand-restored
# copy, so there is no reason to hash a subset of it.
klayout_bin() {
    ( set -o pipefail
      cd "$GATE_KLAYOUT_BIN" && find . -type f -print0 | LC_ALL=C sort -z \
          | xargs -0 -r sha256sum )
}
if ! klayout_bin > /logs/klayout-bin-before.txt \
   || ! grep -q ' \./libklayout_db\.so' /logs/klayout-bin-before.txt; then
    echo "KLAYOUT_HASH_RC=1"; exit 95
fi
echo "KLAYOUT_HASH_RC=0 ($(wc -l < /logs/klayout-bin-before.txt) files)"

# Headers from this tree, libraries from the image: equal headers under every
# include directory, or the binary is built against one KLayout and loaded with
# another and nothing below would say so.
headers() {  # headers <dir>
    ( set -o pipefail
      cd "$1" && find . -maxdepth 1 -type f \( -name '*.h' -o -name '*.hpp' \) -print0 \
          | LC_ALL=C sort -z | xargs -0 -r sha256sum )
}
: > /logs/klayout-headers-tree.txt; : > /logs/klayout-headers-image.txt
HDR_RC=0
while IFS= read -r d; do
    rel="${d#extern/klayout/src}"
    { echo "# $d"; headers "/workspace/$d"; } >> /logs/klayout-headers-tree.txt || HDR_RC=1
    { echo "# $d"; headers "$GATE_KLAYOUT_SRC$rel"; } >> /logs/klayout-headers-image.txt || HDR_RC=1
done < /logs/klayout-include-dirs.txt
grep -q '\.h$' /logs/klayout-headers-tree.txt || HDR_RC=1
if [ "$HDR_RC" -eq 0 ] && diff -q /logs/klayout-headers-tree.txt \
       /logs/klayout-headers-image.txt > /dev/null; then
    echo "KLAYOUT_HEADERS_MATCH_RC=0"
else
    echo "KLAYOUT_HEADERS_MATCH_RC=1"
    diff /logs/klayout-headers-tree.txt /logs/klayout-headers-image.txt | head -20
    exit 96
fi

# The cross-reader check reads the writer's output with PyYAML, and its
# expectations were established against one version of it.
GATE_PY="${CROSS_READER_PYTHON:-/opt/crossreader-venv/bin/python}"
PYYAML_VERSION="$("$GATE_PY" -I -c 'import yaml; print(yaml.__version__)' 2>&1)"
echo "PYYAML_VERSION=$PYYAML_VERSION ($GATE_PY)"
if [ "$PYYAML_VERSION" = "$GATE_PYYAML_PIN" ]; then
    echo "PYYAML_MATCH_RC=0"
else
    echo "PYYAML_MATCH_RC=1 (want $GATE_PYYAML_PIN)"; exit 97
fi

echo "BUILD_START=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
BUILD_START_EPOCH=$(date -u +%s)
make -j"$GATE_JOBS" > /logs/build-output.log 2>&1
MAKE_RC=$?; echo "MAKE_RC=$MAKE_RC"
echo "BUILD_END=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "BUILD_SECONDS=$(( $(date -u +%s) - BUILD_START_EPOCH ))"
if [ "$MAKE_RC" -ne 0 ]; then tail -40 /logs/build-output.log; exit 91; fi

# Hashing the binaries is part of the run, and a failure to hash them is a
# failed run: an unhashed product means the capture cannot say what executed.
if ! products > /logs/product-inventory-before-test.txt; then
    echo "BINARY_HASH_RC=1"; exit 92
fi
sha256sum /usr/bin/ctest >> /logs/product-inventory-before-test.txt
echo "BINARY_HASH_RC=0"
cat /logs/product-inventory-before-test.txt

# ONE xvfb-run, wrapping the inventory AND the run, because both need a
# display and they must have the SAME one.
#
# tests/CMakeLists.txt sets DISCOVERY_MODE PRE_TEST, so ctest does not read a
# test list from anywhere: it ENUMERATES by running chiplet_tests. With
# QT_QPA_PLATFORM=xcb exported for the whole container, an enumeration outside
# a display server aborts in Qt before gtest is reached, and
# `ctest --show-only=json-v1` writes an empty file. That is what happened once:
# MAKE_RC=0, a zero-byte inventory, INVENTORY_RC=1, exit 93. It had
# never fired before only because a chiplet_tests[1]_tests.cmake left by an
# earlier run happened to be newer than the binary, which makes the generated
# include file skip discovery; a rebuild invalidates that, so the reconciler's
# inventory half had been resting on a cache.
#
# Not two xvfb-run calls, and not offscreen for the inventory alone. Two
# `xvfb-run -a` invocations take two DIFFERENT displays, and offscreen would
# enumerate under one Qt platform while executing under another. Either way the
# two artifacts this gate reconciles against each other would have been produced
# under conditions the harness itself made different, which is precisely the
# divergence the reconciliation exists to detect.
#
# -noreset so the server survives a client exiting mid-suite. The server's
# diagnostics are retained by xvfb-run's own -e, which captures the stderr Xvfb
# writes them to.
#
# There is no -logfile here and there must not be. The Xvfb in this image does
# not implement that option at all: it answers "Unrecognized option: -logfile"
# and never starts, while xvfb-run still exports a DISPLAY, so the whole suite
# would have run for its full duration against a display with no server behind
# it. Measured in the image, not read from a manual page. An earlier version of
# this runner passed it, and the note that replaced it was wrong in a milder way
# (it said the option redirected the stream rather than killing the server);
# both are recorded because the second is the kind of plausible correction that
# closes an investigation without fixing it.
echo "TEST_START=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
TEST_START_EPOCH=$(date -u +%s)
xvfb-run -a -e /logs/xvfb-run.err \
  --server-args="-noreset -screen 0 1920x1080x24" \
  bash /logs/display-work.sh
DISPLAY_RC=$?
echo "TEST_END=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "TEST_SECONDS=$(( $(date -u +%s) - TEST_START_EPOCH ))"
# The inventory's own result is read from a file rather than inferred from the
# exit code, because 93 is a number ctest could also return and a verdict must
# not rest on that not happening. The absent case is reported separately from
# the failed case: no sentinel at all means the half under the display never
# started, which is a different fault from an enumeration that ran and failed,
# and the earlier, thinner version of this message is what made the diagnosis
# take a whole extra run to reach.
INVENTORY_SENTINEL="$(cat /logs/inventory-rc.txt 2>/dev/null || true)"
if [ -z "$INVENTORY_SENTINEL" ]; then
    echo "INVENTORY_RC=absent"
    echo "DISPLAY_HALF_DID_NOT_RUN: xvfb-run exited $DISPLAY_RC without"
    echo "  display-work.sh recording a result. See /logs/xvfb-run.err."
    exit 93
fi
if [ "$INVENTORY_SENTINEL" != "0" ]; then
    exit 93
fi
CTEST_RC=$DISPLAY_RC

# After TEST, not just after build. A test that rewrote a binary it also
# executed would otherwise be invisible, and the before-build/after-build pair
# says nothing about the window the tests actually ran in.
if ! products > /logs/product-inventory-after-test.txt; then
    echo "POST_TEST_HASH_RC=1"; exit 94
fi
sha256sum /usr/bin/ctest >> /logs/product-inventory-after-test.txt
if diff -q /logs/product-inventory-before-test.txt \
           /logs/product-inventory-after-test.txt > /dev/null; then
    echo "PRODUCTS_UNCHANGED_RC=0"
else
    echo "PRODUCTS_UNCHANGED_RC=1"
    diff /logs/product-inventory-before-test.txt /logs/product-inventory-after-test.txt
fi
if klayout_bin > /logs/klayout-bin-after.txt \
   && diff -q /logs/klayout-bin-before.txt /logs/klayout-bin-after.txt > /dev/null; then
    echo "KLAYOUT_UNCHANGED_RC=0"
else
    echo "KLAYOUT_UNCHANGED_RC=1"
    diff /logs/klayout-bin-before.txt /logs/klayout-bin-after.txt | head -20
fi
exit $CTEST_RC
WORK
chmod +x "$LOGDIR/inner-work.sh" || exit 2

# The half that runs under the display, in its own file so the capture records
# it by hash like everything else the run executes.
cat > "$LOGDIR/display-work.sh" <<'DISPLAYWORK' || exit 2
#!/bin/bash
set -u
cd /workspace/build || exit 90

# The inventory: what ctest resolves from this build. PRE_TEST discovery means
# resolving it RUNS the test binary, so this needs the display as much as the
# run below does, and it is here rather than outside for that reason.
if ! ctest --show-only=json-v1 > /logs/ctest-inventory.json 2> /logs/ctest-inventory.err; then
    echo 1 > /logs/inventory-rc.txt
    echo "INVENTORY_RC=1"; cat /logs/ctest-inventory.err; exit 93
fi
echo 0 > /logs/inventory-rc.txt
echo "INVENTORY_RC=0"

ctest --output-on-failure --output-junit /logs/ctest-results.xml
CTEST_RC=$?
echo "CTEST_RC=$CTEST_RC"
exit $CTEST_RC
DISPLAYWORK
chmod +x "$LOGDIR/display-work.sh" || exit 2
# Fail-closed like the rest of the capture's bookkeeping. Unhashed scripts mean
# the capture cannot say what it executed, and until now this could fail and the
# run still reach GREEN.
( cd "$LOGDIR" && sha256sum inner-work.sh display-work.sh \
    >> runner-as-executed.sh.sha256 ) || {
  echo "ABORT: cannot record the hashes of the generated container scripts"
  exit 2; }

INNER='
  timeout --signal=INT --kill-after=60 '"$INNER_TIMEOUT"' bash /logs/inner-work.sh
  RC=$?
  echo "INNER_RC=$RC"
  if [ "$RC" -eq 124 ] || [ "$RC" -eq 137 ]; then
    echo "INNER_TIMEOUT_FIRED: the whole container workload hit '"$INNER_TIMEOUT"'s"
  fi
  exit $RC
'

# --network none is part of the invocation, not a hardening option. The build is
# incremental over a dependency tree that is already fetched, and the suite reads
# only what is mounted, so nothing here has any business reaching the network. If
# something does, the run must fail rather than quietly succeed by downloading
# it: a gate that can fetch is a gate whose result depends on what a server
# happened to serve that minute, and the capture would record none of it.
ARGS=(
  run --rm --name "$CONTAINER" "--cpus=$CPUS" "--memory=$MEM" --network none
  -v "$REPO":/workspace -v "$LOGDIR":/logs -w /workspace
  -e QT_QPA_PLATFORM=xcb
  -e GATE_JOBS="$JOBS"
  -e LD_LIBRARY_PATH="$KLAYOUT_BIN:$KLAYOUT_BIN/db_plugins"
  -e GATE_KLAYOUT_BIN="$KLAYOUT_BIN"
  -e GATE_KLAYOUT_SRC="$KLAYOUT_IMAGE_SRC"
  -e GATE_PYYAML_PIN="$PYYAML_PIN"
  -u "$(id -u):$(id -g)"   # root skips the two AtomicSave tests that pin save safety
)
# Read-only, and only when the cache asked for it. In an in-tree configure the
# dependency sources are already inside the /workspace mount and this adds
# nothing; the mapping file records which of the two shapes a run was.
if [ -n "$DEPS_PREFIX" ]; then
  ARGS+=( -v "$DEPS_HOST_IN":"$DEPS_PREFIX":ro )
fi
[ -n "${CROSS_READER_PYTHON:-}" ] && ARGS+=( -e CROSS_READER_PYTHON="$CROSS_READER_PYTHON" )
if [ -n "$FIXTURE" ] && [ -f "$FIXTURE" ]; then
  ARGS+=( -v "$FIXTURE":/wirebond/demo.chiplet:ro
          -e WIREBOND_DEMO_CHIPLET=/wirebond/demo.chiplet )
fi
for sib in interposer interconnect_pdk; do
  [ -d "$ROOT/$sib" ] && ARGS+=( -v "$ROOT/$sib":/opt/pdks/$sib:ro )
done
[ -d "$ROOT/gds_to_kicad/pdks" ] && \
  ARGS+=( -v "$ROOT/gds_to_kicad/pdks":/opt/pdks/gds_to_kicad/pdks:ro )
[ -d "$ROOT/gds_to_kicad/gds_files/for_thermal_eval/Metal_Test" ] && \
  ARGS+=( -v "$ROOT/gds_to_kicad/gds_files/for_thermal_eval/Metal_Test":/opt/pdks/gds_to_kicad/gds_files/for_thermal_eval/Metal_Test:ro )
ARGS+=( -e GDS_TO_KICAD_ROOT=/opt/pdks/gds_to_kicad
        -e INTERPOSER_PDK_ROOT=/opt/pdks/interposer
        -e INTERCONNECT_PDK_ROOT=/opt/pdks/interconnect_pdk
        "$IMAGE_ID" bash -c "$INNER" )

printf '%s\n' docker "${ARGS[@]}" > "$LOGDIR/docker-argv.txt"
echo "=== gate (client timeout ${RUN_TIMEOUT}s, ctest timeout ${INNER_TIMEOUT}s) ==="
RUN_START_ISO="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
RUN_START_EPOCH="$(date -u +%s)"
# On disk BEFORE docker is invoked, so a run killed mid-container still says
# when it started.
stamp run_start "$RUN_START_ISO"
echo "run start $RUN_START_ISO (date -u, on the host)"
timeout --signal=INT --kill-after=120 "$RUN_TIMEOUT" docker "${ARGS[@]}" > "$LOG" 2>&1
CLIENT_RC=$?
# Sampled the instant the client returns, whatever that return was, so the
# recorded end is the real one. WRITING them waits until after the cleanup
# below: `stamp` is fatal by design, and on the timeout path a failed disk write
# here would have exited before the container was killed, leaving a container
# running to protect a timestamp. Cleanup outranks bookkeeping.
RUN_END_ISO="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
RUN_END_EPOCH="$(date -u +%s)"

# The client's exit and the container's are different facts and are recorded
# separately. 124 here means the CLIENT was aborted; it says nothing about
# whether the workload stopped.
echo "$CLIENT_RC" > "$LOGDIR/client-exit.txt"
if [ "$CLIENT_RC" -eq 124 ]; then
  echo "CLIENT_TIMEOUT: the ${RUN_TIMEOUT}s client timeout fired"
  docker kill "$CONTAINER" >/dev/null 2>&1 && echo "killed container $CONTAINER"
fi
# Cleanup verification. A container of ours that is still running holds its
# CPUs and memory, whatever this script exits with.
RESIDUAL="$(docker ps --filter "name=$CONTAINER" --format '{{.ID}} {{.Status}}')"
{ echo "client_exit $CLIENT_RC"
  echo "container_name $CONTAINER"
  echo "residual [${RESIDUAL:-none}]"
} > "$LOGDIR/cleanup-verification.txt"
cat "$LOGDIR/cleanup-verification.txt"
if [ -n "$RESIDUAL" ]; then
  echo "WARNING: $CONTAINER is STILL RUNNING and holding its CPUs and memory."
  echo "         docker kill $CONTAINER"
fi
# Now that the container is dealt with and the residual check is on disk, the
# fatal stamps for the run are safe to write.
stamp run_end "$RUN_END_ISO"
stamp run_seconds "$((RUN_END_EPOCH - RUN_START_EPOCH))"

cp "$REPO/build/CMakeCache.txt" "$LOGDIR/CMakeCache.txt" 2>/dev/null

# --- source inventory, AFTER -------------------------------------------------
{ echo "HEAD $(git rev-parse HEAD)"
  echo "tree $(git rev-parse HEAD^{tree})"
  echo "dirty [$(git status --porcelain | tr '\n' ';')]"
} > "$LOGDIR/source-inventory-after.txt"
source_manifest "$LOGDIR/source-manifest-after.txt" || \
  echo "(the after manifest could not be produced)" > "$LOGDIR/source-manifest-after.txt"
# A failure to produce either AFTER inventory writes a line no BEFORE inventory
# can contain, so the comparison below reddens rather than being skipped.
external_manifest "$LOGDIR/external-manifest-after.txt" || \
  echo "(the after inventory of the external inputs could not be produced)" \
    > "$LOGDIR/external-manifest-after.txt"

# --- verdict: the reconciliation, not the log -------------------------------
echo "=== verdict ==="
GREEN=1
# Both actual exits, not just a valid report. A reconciliation over well-formed
# artifacts says the tests that ran agree with the tests that were resolved; it
# does not say the client and ctest themselves returned normally, and a run that
# was aborted, signalled or timed out is not a run this gate accepts however
# tidy its XML looks.
[ "$CLIENT_RC" -eq 0 ] || { echo "the docker client exited $CLIENT_RC"; GREEN=0; }
grep -q "^CTEST_RC=0" "$LOG" || { echo "ctest did not exit 0"; GREEN=0; }
grep -q "^INNER_RC=0" "$LOG" || { echo "the container workload did not exit 0"; GREEN=0; }
grep -q "^INNER_TIMEOUT_FIRED" "$LOG" && { echo "the in-container timeout fired"; GREEN=0; }

# The source must be the same source, still clean, and byte-identical at the end.
[ "$(git rev-parse HEAD)" = "$SHA" ] || { echo "HEAD moved during the run"; GREEN=0; }
[ -z "$(git status --porcelain)" ]   || { echo "the tree is dirty after the run"; GREEN=0; }
if ! diff -q "$LOGDIR/source-manifest-before.txt" \
             "$LOGDIR/source-manifest-after.txt" > /dev/null 2>&1; then
  echo "the source or dependency inputs changed during the run:"
  diff "$LOGDIR/source-manifest-before.txt" "$LOGDIR/source-manifest-after.txt" | head -20
  GREEN=0
fi
if ! diff -q "$LOGDIR/external-manifest-before.txt" \
             "$LOGDIR/external-manifest-after.txt" > /dev/null 2>&1; then
  echo "the external inputs (PDK trees, fixture, GDS assets) changed during the run:"
  diff "$LOGDIR/external-manifest-before.txt" "$LOGDIR/external-manifest-after.txt" | head -20
  GREEN=0
fi

grep -q "^MAKE_RC=0" "$LOG"             || { echo "build not green"; GREEN=0; }
grep -q "^BINARY_HASH_RC=0" "$LOG"      || { echo "the products were not hashed"; GREEN=0; }
grep -q "^INVENTORY_RC=0" "$LOG"        || { echo "ctest produced no inventory"; GREEN=0; }
grep -q "^PRODUCTS_UNCHANGED_RC=0" "$LOG" || { echo "the binaries changed across the test run, or were not re-hashed"; GREEN=0; }
grep -q "^KLAYOUT_HASH_RC=0" "$LOG"     || { echo "the image's KLayout was not hashed"; GREEN=0; }
grep -q "^KLAYOUT_HEADERS_MATCH_RC=0" "$LOG" || { echo "this tree's KLayout headers differ from the image's"; GREEN=0; }
grep -q "^PYYAML_MATCH_RC=0" "$LOG"     || { echo "the cross-reader PyYAML is not $PYYAML_PIN"; GREEN=0; }
grep -q "^KLAYOUT_UNCHANGED_RC=0" "$LOG" || { echo "the image's KLayout changed across the run, or was not re-hashed"; GREEN=0; }

RECON_ARGS=(--inventory "$LOGDIR/ctest-inventory.json"
            --junit "$LOGDIR/ctest-results.xml"
            --require "$LOGDIR/gate_required_tests.txt")
[ "$ALLOW_SKIPS" -eq 1 ] && RECON_ARGS+=(--allow-skips)
# Not `python3 ... | tee`: a pipeline's status is the LAST command's, so tee
# would report success for a reconciler that failed, which is the exact shape of
# bug this whole file exists to remove.
python3 "$LOGDIR/reconcile_gate.py" "${RECON_ARGS[@]}" > "$LOGDIR/verdict.txt" 2>&1
RECON_RC=$?
cat "$LOGDIR/verdict.txt"
echo "RECONCILE_RC=$RECON_RC" >> "$LOGDIR/verdict.txt"
[ "$RECON_RC" -eq 0 ] || GREEN=0
[ -n "$RESIDUAL" ] && GREEN=0

# Appended, not assembled: everything above is already on disk, and reaching
# here is the only thing that entitles the capture to an end.
stamp gate_end "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
stamp gate_seconds "$(( $(date -u +%s) - GATE_START_EPOCH ))"
printf '# container stamps are in gate.log: BUILD_START, BUILD_END,\n# BUILD_SECONDS, TEST_START, TEST_END, TEST_SECONDS, all date -u\n' \
  >> "$STAMPS" || { echo "ABORT: cannot append the container stamp note"; exit 2; }
cat "$STAMPS"

echo "capture: $LOGDIR"
[ "$GREEN" -eq 1 ] && { echo "GATE GREEN"; exit 0; }
echo "GATE NOT GREEN"; exit 1
