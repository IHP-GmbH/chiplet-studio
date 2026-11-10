#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 IHP GmbH
#
# test_demo_entrypoints.sh - do the demo launchers pass what they claim to?
#
# WHAT THIS TESTS. The argv the launcher hands to docker, the environment flags
# in it, that the compatibility name forwards arguments and environment
# unchanged, and that a failing launch's exit status reaches the caller. That
# is all. It starts no container, opens no display and renders nothing: docker
# and xhost are replaced on PATH by mocks that record their argv and exit with
# whatever the case asks for.
#
# WHY MOCKED AND NOT REAL. A real launch needs an X display, a built binary and
# a mutable image, and it would then prove that a GUI came up rather than that
# the launcher passed the right flags. The defect these controls exist for was
# a missing -e flag, which is visible in argv and nowhere else.
#
# WHAT A PASS HERE DOES NOT MEAN. It does not mean the studio finds its module
# at runtime; that was measured separately by the relocation probes, under a
# pinned image and read-only mounts, and nothing about a mocked argv check
# transfers that result.
#
# WHERE IT RUNS. The contracts job in .github/workflows/ci.yml invokes this file
# directly, and tests/contracts/ runs it under pytest for a host with no build.
# It needs no CMake, no native build and no PDK checkout, so it costs seconds and
# does not touch the ctest census. Not registering it with ctest is about what
# the runner can do, not about avoiding a number.
#
# IT SUPPLIES ITS OWN PDK DIRECTORIES. scripts/pdk-env.sh derives host PDK paths
# from HOME, so a run that inherited them would measure this machine's checkout
# layout rather than the launcher. These controls create their own directories
# and name them explicitly, which keeps them independent of the host without
# reassigning HOME. That also lets them drive the absent cases on purpose: the
# eight-combination block below owns every path it removes.
#
# Usage: tests/shell/test_demo_entrypoints.sh
set -u

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SCRIPTS="$REPO/scripts"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PASS=0; FAILED=0
ok()  { echo "  ok   $1"; PASS=$((PASS + 1)); }
bad() { echo "  FAIL $1"; FAILED=$((FAILED + 1)); }

# --- the mocks. One docker, one xhost, both recording, neither doing anything --
mkdir -p "$WORK/bin"
cat > "$WORK/bin/docker" <<'MOCK'
#!/bin/bash
printf '%s\n' "$@" > "$MOCK_ARGV"
printf 'PROJECT_ROOT=%s\n' "${PROJECT_ROOT:-}" >> "$MOCK_ENV"
exit "${MOCK_DOCKER_EXIT:-0}"
MOCK
cat > "$WORK/bin/xhost" <<'MOCK'
#!/bin/bash
echo "$@" >> "$MOCK_XHOST"
exit 0
MOCK
chmod +x "$WORK/bin/docker" "$WORK/bin/xhost"

# A project root shaped like the real one, so the launcher's path derivation and
# its file check run for real rather than being stubbed out.
FAKE="$WORK/project"
mkdir -p "$FAKE/adk-tools/examples/two_die_interposer/outputs"
DEMO="$FAKE/adk-tools/examples/two_die_interposer/outputs/two_die_interposer.chiplet"
echo "format_version: \"1.0\"" > "$DEMO"
OTHER="$WORK/another.chiplet"; echo "format_version: \"1.0\"" > "$OTHER"

# PDK directories this test owns, so the launchers' pdk-env.sh finds all three
# and the run does not depend on what this machine happens to have checked out.
PDKS="$WORK/pdks"
mkdir -p "$PDKS/interconnect_pdk" "$PDKS/interposer" "$PDKS/ihp"

# An assertion about a FLAG, not about a string that happens to be in argv. The
# launchers already pass -e twice for other variables, so grepping for "-e" and
# for the value separately proves only that the value appears somewhere. This
# requires the value to be the argument immediately following its flag.
argv_pair() {  # argv_pair <flag> <value> <argv file>
  awk -v f="$1" -v v="$2" 'prev == f && $0 == v { found = 1 } { prev = $0 }
                           END { exit !found }' "$3"
}

run_launcher() {  # run_launcher <script> [args...]; sets RC and ARGV_FILE
  local script="$1"; shift
  ARGV_FILE="$WORK/argv.txt"; ENV_FILE="$WORK/env.txt"; XHOST_FILE="$WORK/xhost.txt"
  : > "$ARGV_FILE"; : > "$ENV_FILE"; : > "$XHOST_FILE"
  PATH="$WORK/bin:$PATH" \
  MOCK_ARGV="$ARGV_FILE" MOCK_ENV="$ENV_FILE" MOCK_XHOST="$XHOST_FILE" \
  MOCK_DOCKER_EXIT="${MOCK_DOCKER_EXIT:-0}" \
  PROJECT_ROOT="$FAKE" \
  HOST_INTERCONNECT_PDK="${PDK_IC:-$PDKS/interconnect_pdk}" \
  HOST_INTERPOSER_PDK="${PDK_IP:-$PDKS/interposer}" \
  HOST_IHP_PDK="${PDK_IHP:-$PDKS/ihp}" \
    bash "$script" "$@" > "$WORK/out.txt" 2>&1
  RC=$?
}

echo "== the flag this change exists for =="
MOCK_DOCKER_EXIT=0 run_launcher "$SCRIPTS/run-chiplet-demo.sh"
if argv_pair "-e" "PYTHONPATH=/workspace/build/python" "$ARGV_FILE"; then
  ok "the canonical launcher passes -e PYTHONPATH=/workspace/build/python as one flag"
else
  bad "the canonical launcher does not pass -e PYTHONPATH as an adjacent pair"; cat "$ARGV_FILE"
fi
argv_pair "-e" "LD_LIBRARY_PATH=/workspace/extern/klayout/bin-release:/workspace/extern/klayout/bin-release/db_plugins" "$ARGV_FILE" &&
  ok "and still passes -e LD_LIBRARY_PATH as it always did" ||
  bad "the KLayout library path went missing or is no longer attached to its flag"
# The value has to match where the module actually lands under this mount, or
# the flag is present and wrong, which is worse than absent.
argv_pair "-v" "$REPO:/workspace" "$ARGV_FILE" &&
  ok "the studio is mounted at /workspace, so /workspace/build/python is that mount's module dir" ||
  bad "the studio is not mounted at /workspace; the PYTHONPATH value would be wrong"
argv_pair "-v" "$PDKS/interposer:/opt/pdks/interposer:ro" "$ARGV_FILE" &&
  ok "and the PDK mounts are attached to their own -v flags" ||
  bad "a PDK mount is missing or detached from its flag"

echo "== with no argument, the example the umbrella ships is opened =="
MOCK_DOCKER_EXIT=0 run_launcher "$SCRIPTS/run-chiplet-demo.sh"
[ "$RC" -eq 0 ] && [ "$(tail -n 1 "$ARGV_FILE")" = "$DEMO" ] &&
  ok "the canonical launcher opens examples/two_die_interposer by default" ||
  { bad "the default document is not the shipped example (exit $RC)"; tail -n 3 "$WORK/out.txt"; }

echo "== the compatibility name forwards, and does not diverge =="
MOCK_DOCKER_EXIT=0 run_launcher "$SCRIPTS/run-chiplet-demo.sh"; cp "$ARGV_FILE" "$WORK/canonical.txt"
MOCK_DOCKER_EXIT=0 run_launcher "$SCRIPTS/run-wirebond-demo.sh"
if diff -q "$WORK/canonical.txt" "$ARGV_FILE" > /dev/null; then
  ok "wirebond produces byte-identical docker argv to the canonical launcher"
else
  bad "wirebond and the canonical launcher disagree"; diff "$WORK/canonical.txt" "$ARGV_FILE" | head -10
fi

echo "== arguments and environment reach the container =="
MOCK_DOCKER_EXIT=0 run_launcher "$SCRIPTS/run-wirebond-demo.sh" "$OTHER"
grep -qx -- "$OTHER" "$ARGV_FILE" &&
  ok "a .chiplet passed to the compatibility name reaches the container command" ||
  bad "the argument was dropped on the way through the delegate"
grep -qx "PROJECT_ROOT=$FAKE" "$ENV_FILE" &&
  ok "the exported environment survives the delegate" ||
  bad "the environment did not survive the delegate"

echo "== exit status is the launch's own =="
MOCK_DOCKER_EXIT=7 run_launcher "$SCRIPTS/run-chiplet-demo.sh"
[ "$RC" -eq 7 ] && ok "the canonical launcher returns the container's exit (7)" ||
  bad "the canonical launcher returned $RC, not 7"
MOCK_DOCKER_EXIT=7 run_launcher "$SCRIPTS/run-wirebond-demo.sh"
[ "$RC" -eq 7 ] && ok "the delegate returns it too, unremapped" ||
  bad "the delegate returned $RC, not 7"

echo "== the black-box entry point, which is not a delegate =="
# Its paths come from its own location: it opens the bundled document of the
# checkout it sits in, mounts that checkout at /workspace and the directory
# holding it as the project root. It honours no PROJECT_ROOT, so the $FAKE this
# harness exports must not reach its mounts, and nothing here depends on HOME.
BB_DOC="$REPO/examples/blackbox/blackbox_demo.chiplet"
PARENT="$(cd "$REPO/.." && pwd)"
MOCK_DOCKER_EXIT=0 run_launcher "$SCRIPTS/run-blackbox-demo.sh"
[ "$RC" -eq 0 ] && ok "the black-box launcher reaches docker and returns its exit (0)" ||
  { bad "the black-box launcher exited $RC"; tail -n 3 "$WORK/out.txt"; }
if argv_pair "-e" "PYTHONPATH=/workspace/build/python" "$ARGV_FILE"; then
  ok "the black-box launcher passes -e PYTHONPATH=/workspace/build/python as one flag"
else
  bad "the black-box launcher does not pass -e PYTHONPATH as an adjacent pair"; cat "$ARGV_FILE"
fi
argv_pair "-v" "$REPO:/workspace" "$ARGV_FILE" &&
  ok "and mounts this checkout at /workspace, so that value is right for this mount too" ||
  bad "the black-box launcher does not mount this checkout at /workspace"
argv_pair "-v" "$PARENT:$PARENT" "$ARGV_FILE" &&
  ok "and mounts the directory holding this checkout as the project root" ||
  { bad "the black-box project root is not the directory holding this checkout"; grep -x -A1 -e "-v" "$ARGV_FILE"; }
[ "$(tail -n 1 "$ARGV_FILE")" = "$BB_DOC" ] &&
  ok "and opens this checkout's own bundled document" ||
  bad "the black-box document is not this checkout's: $(tail -n 1 "$ARGV_FILE")"

echo "== an absent optional PDK warns; it does not end the launch =="
# Every PDK is optional, so none of the eight present/absent combinations may
# fail the run, each must mount exactly the ones that exist, and each must say
# which are missing. All eight and not just the interesting one, because the
# defect this replaces was POSITIONAL: only the PDK that happened to sit last in
# pdk-env.sh could kill the launcher. A control pinned to today's last line
# would pass again the moment someone reorders them.
MISSING="$WORK/no-such-pdk"   # never created
[ -e "$MISSING" ] && { bad "the absent-PDK fixture exists; the absent cases prove nothing"; }
for mask in 0 1 2 3 4 5 6 7; do
  ic=$(( mask & 1 )); ip=$(( (mask >> 1) & 1 )); ihp=$(( (mask >> 2) & 1 ))
  [ "$ic" = 1 ]  && PDK_IC="$PDKS/interconnect_pdk" || PDK_IC="$MISSING-ic"
  [ "$ip" = 1 ]  && PDK_IP="$PDKS/interposer"       || PDK_IP="$MISSING-ip"
  [ "$ihp" = 1 ] && PDK_IHP="$PDKS/ihp"             || PDK_IHP="$MISSING-ihp"
  export PDK_IC PDK_IP PDK_IHP
  label="interconnect=$ic interposer=$ip ihp=$ihp"
  MOCK_DOCKER_EXIT=0 run_launcher "$SCRIPTS/run-chiplet-demo.sh"

  [ "$RC" -eq 0 ] && ok "$label: the launch is not refused" ||
    { bad "$label: an absent optional PDK ended the launch (rc $RC)"; head -3 "$WORK/out.txt"; }

  # Exactly the present ones are mounted: present implies a -v pair, absent
  # implies no mount naming that path at all, so a wrong-but-present mount is
  # caught as well as a missing one.
  mounted=0
  for spec in "$ic:$PDK_IC:/opt/pdks/interconnect_pdk" \
              "$ip:$PDK_IP:/opt/pdks/interposer" \
              "$ihp:$PDK_IHP:/opt/pdks/ihp-sg13g2"; do
    want="${spec%%:*}"; rest="${spec#*:}"; host="${rest%:*}"; cpath="${rest##*:}"
    if argv_pair "-v" "$host:$cpath:ro" "$ARGV_FILE"; then found=1; else found=0; fi
    [ "$found" = "$want" ] || { mounted=1; echo "    ($cpath: wanted $want, argv says $found)"; }
  done
  [ "$mounted" -eq 0 ] && ok "$label: exactly the PDKs that exist are mounted" ||
    bad "$label: the mount set does not match which directories exist"

  # And it says so. A silent fallback to generic colors is the failure mode
  # here: the studio comes up and renders the wrong thing.
  warned=0
  [ "$ic" = 0 ]  && { grep -q "interconnect PDK not found" "$WORK/out.txt" || warned=1; }
  [ "$ip" = 0 ]  && { grep -q "interposer PDK not found"   "$WORK/out.txt" || warned=1; }
  [ "$ihp" = 0 ] && { grep -q "IHP SG13G2 PDK not found"   "$WORK/out.txt" || warned=1; }
  [ "$warned" -eq 0 ] && ok "$label: each absent PDK is named in a warning" ||
    { bad "$label: an absent PDK went unmentioned"; cat "$WORK/out.txt"; }
done
unset PDK_IC PDK_IP PDK_IHP

echo "== a missing document is refused before anything is launched =="
MOCK_DOCKER_EXIT=0 run_launcher "$SCRIPTS/run-chiplet-demo.sh" "$WORK/does-not-exist.chiplet"
[ "$RC" -eq 1 ] && ok "a missing .chiplet exits 1" || bad "a missing .chiplet exited $RC"
# An exit 1 is not enough on its own. Until this increment, a host without the
# PDK directories made sourcing pdk-env.sh return non-zero and set -e ended the
# run before the document was ever looked at: same exit code, entirely different
# reason, and this control passed while measuring nothing. The if/else blocks
# removed that particular route, but "exit 1" stays a weak claim, so the reason
# is asserted rather than assumed.
grep -q "chiplet file not found" "$WORK/out.txt" &&
  ok "and it exits for THAT reason, named in its own message" ||
  { bad "exit 1 came from somewhere else; the control would have passed for the wrong reason"
    head -3 "$WORK/out.txt"; }
# Refused BEFORE anything is launched: no container and no display grant. xhost
# sits between the file check and docker run, so checking only docker would miss
# a refusal that had already touched the host's X access control.
[ ! -s "$ARGV_FILE" ]  && ok "and docker was never invoked" ||
  bad "docker ran for a file that does not exist"
[ ! -s "$XHOST_FILE" ] && ok "and xhost was never invoked either" ||
  { bad "the display was opened up before the document was even checked"; cat "$XHOST_FILE"; }

echo
echo "passed $PASS, failed $FAILED"
[ "$FAILED" -eq 0 ]
