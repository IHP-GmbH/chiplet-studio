#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 IHP GmbH
#
# configure-gate.sh - configure build/ for scripts/run-gate.sh, in the image the
# gate runs in.
#
# The gate does not configure: it runs offline over a build/ that already
# exists, and reads from its cache which KLayout to load and where the fetched
# dependencies live. This is the step that makes that cache, and it is the only
# step that reaches the network: FetchContent clones each dependency at the
# commit its FetchContent_Declare pins. It then checks that every fetched
# checkout is at exactly that commit, so a pin that does not hold is a failed
# configure and not a green gate over whatever the server returned.
#
# Fresh by construction: an existing build/ is refused rather than reused, since
# a cache carried over from another image or another KLayout is the thing the
# gate's cache checks exist to catch, and catching it here is cheaper.
#
# Usage:
#   CHIPLET_GATE_IMAGE=<adk-tools studio-builder image> ./scripts/configure-gate.sh
#
# Environment:
#   CHIPLET_GATE_IMAGE       docker image, REQUIRED (the one run-gate.sh will use)
#   CHIPLET_GATE_KLAYOUT_BIN the image's KLayout build directory (default: where
#                            the adk-tools studio-builder stage builds it)
#   CHIPLET_GATE_CPUS        cpu limit (default: 6)
#   CHIPLET_GATE_MEM         memory limit (default: 12g)
set -u

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${CHIPLET_GATE_IMAGE:-}"
KLAYOUT_BIN="${CHIPLET_GATE_KLAYOUT_BIN:-/opt/adk-tools/chiplet-studio/extern/klayout/bin-release}"
CPUS="${CHIPLET_GATE_CPUS:-6}"
MEM="${CHIPLET_GATE_MEM:-12g}"

[ -n "$IMAGE" ] || { echo "ABORT: CHIPLET_GATE_IMAGE is unset." >&2; exit 2; }
docker image inspect "$IMAGE" > /dev/null 2>&1 || {
  echo "ABORT: no docker image $IMAGE" >&2; exit 2; }
cd "$REPO" || exit 1
[ ! -e build ] || {
  echo "ABORT: build/ exists. This configures from nothing; move it aside." >&2; exit 2; }

# The pins, read from the files that declare them: name and commit of every
# FetchContent_Declare. Read here rather than listed, for the reason run-gate.sh
# reads the cache: a dependency added later cannot be left unchecked by being
# missing from a list.
PINS="$(awk '
  /FetchContent_Declare\(/ { name = ""; want = 1; next }
  want && name == "" && NF { name = $1; next }
  want && $1 == "GIT_TAG" { print name, $2; want = 0 }
' CMakeLists.txt tests/CMakeLists.txt)"
[ -n "$PINS" ] || { echo "ABORT: found no FetchContent_Declare pin." >&2; exit 2; }
while read -r name sha; do
  case "$sha" in
    [0-9a-f]*) [ "${#sha}" -eq 40 ] && [ -z "${sha//[0-9a-f]/}" ] && continue;;
  esac
  echo "ABORT: $name is declared at '$sha', not a commit id. A tag can move." >&2
  exit 2
done <<< "$PINS"
echo "pins:"; echo "$PINS" | sed 's/^/  /'

docker run --rm "--cpus=$CPUS" "--memory=$MEM" \
  -v "$REPO":/workspace -w /workspace -u "$(id -u):$(id -g)" -e HOME=/tmp \
  "$IMAGE" bash -c '
    set -e
    mkdir build && cd build
    cmake .. -DKLAYOUT_BUILD_DIR="'"$KLAYOUT_BIN"'" -DCHIPLET_REQUIRE_KLAYOUT=ON \
             -DENABLE_PYTHON_SCRIPTING=ON
  ' || { echo "ABORT: configure failed in $IMAGE" >&2; exit 1; }

# Each fetched checkout must be at its pin. FetchContent names the checkout
# <lowercased name>-src under build/_deps.
FAIL=0
: > build/fetched-heads.txt
while read -r name sha; do
  dir="build/_deps/$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')-src"
  head="$(git -C "$dir" rev-parse HEAD 2>/dev/null || echo missing)"
  echo "$name $head (declared $sha)" | tee -a build/fetched-heads.txt
  [ "$head" = "$sha" ] || { echo "MISMATCH: $name fetched $head, declared $sha"; FAIL=1; }
done <<< "$PINS"
[ "$FAIL" -eq 0 ] || exit 1
echo "configured: every dependency fetched at its declared commit"
