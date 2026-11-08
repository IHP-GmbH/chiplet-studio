# pdk-env.sh - Shared PDK host->container mount mapping for the run-*.sh launchers.
#
# Source this from a launcher (it is not meant to be executed directly).
#
# Why this exists: the chiplet-studio-build image bakes the PDK roots as env
# vars at FIXED container paths (docker/Dockerfile.build), so the image carries
# no host-specific paths and other users can run it without this host's layout.
# Each launcher mounts its host PDK dirs onto those fixed paths; resolution
# inside the app reads the env vars first (env -> textvar -> sibling walk).
# Without the interconnect PDK mounted/resolved, CuPillar/bump bodies do not
# render in the interposer's Detailed view.
#
# Override the HOST_* vars below if your local PDK checkouts live elsewhere.
#
# IMPORTANT (docker vs host): these env vars live INSIDE the image. When you run
# the tools directly on the host (not via Docker), export INTERCONNECT_PDK_ROOT /
# INTERPOSER_PDK_ROOT / PDK_ROOT yourself -- the image's ENV does not apply.

# Host-side PDK locations (override per machine). The default project root is
# the folder holding this checkout, where the sibling PDK checkouts live.
: "${HETERO_PROJECT:=${PROJECT_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}}"
: "${HOST_INTERCONNECT_PDK:=${HETERO_PROJECT}/interconnect_pdk}"
: "${HOST_INTERPOSER_PDK:=${HETERO_PROJECT}/interposer}"
: "${HOST_IHP_PDK:=${HOME}/git/ihp_pdk}"

# Fixed container mount points -- MUST match the ENV in docker/Dockerfile.build
CONTAINER_INTERCONNECT_PDK=/opt/pdks/interconnect_pdk
CONTAINER_INTERPOSER_PDK=/opt/pdks/interposer
CONTAINER_IHP_PDK=/opt/pdks/ihp-sg13g2

# Build the -v mount flags (only for PDK dirs that exist on the host).
#
# Every PDK here is OPTIONAL: an absent one costs rendering, not the launch. So
# each is its own if/else, and the file never ends on a test whose result the
# caller inherits.
#
# That last part is not style. A sourced file returns the status of its LAST
# command, and the launchers source this under set -e, so a trailing
# `[ -d "$dir" ] && PDK_MOUNTS+=(...)` made the launcher exit 1 when that one
# directory was absent, before it had looked at the document it was asked to
# open. The bug was POSITIONAL, not per-line: an absent middle PDK was harmless
# (a bare AND-list whose left side fails is exempt from errexit mid-file) and
# only whichever one sat last could kill the run. Reordering the three moved it;
# appending a fourth would have moved it again. if/else blocks end on an echo or
# an array append and cannot carry a failure out, whatever order they are in.
PDK_MOUNTS=()
if [ -d "$HOST_INTERCONNECT_PDK" ]; then
    PDK_MOUNTS+=( -v "${HOST_INTERCONNECT_PDK}:${CONTAINER_INTERCONNECT_PDK}:ro" )
else
    echo "warning: interconnect PDK not found at $HOST_INTERCONNECT_PDK" >&2
    echo "         CuPillar/bump bodies will not render (set HOST_INTERCONNECT_PDK)." >&2
fi
if [ -d "$HOST_INTERPOSER_PDK" ]; then
    PDK_MOUNTS+=( -v "${HOST_INTERPOSER_PDK}:${CONTAINER_INTERPOSER_PDK}:ro" )
else
    echo "warning: interposer PDK not found at $HOST_INTERPOSER_PDK" >&2
    echo "         interposer layers fall back to generic colors (set HOST_INTERPOSER_PDK)." >&2
fi
if [ -d "$HOST_IHP_PDK" ]; then
    PDK_MOUNTS+=( -v "${HOST_IHP_PDK}:${CONTAINER_IHP_PDK}:ro" )
else
    echo "warning: IHP SG13G2 PDK not found at $HOST_IHP_PDK" >&2
    echo "         die layers fall back to generic colors (set HOST_IHP_PDK)." >&2
fi
