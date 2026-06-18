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

# Host-side PDK locations (override per machine)
: "${HETERO_PROJECT:=${PROJECT_ROOT:-/home/montanares/git/heterogenic_chip_design_project}}"
: "${HOST_INTERCONNECT_PDK:=${HETERO_PROJECT}/interconnect_pdk}"
: "${HOST_INTERPOSER_PDK:=${HETERO_PROJECT}/interposer}"
: "${HOST_IHP_PDK:=/home/montanares/git/ihp_pdk}"

# Fixed container mount points -- MUST match the ENV in docker/Dockerfile.build
CONTAINER_INTERCONNECT_PDK=/opt/pdks/interconnect_pdk
CONTAINER_INTERPOSER_PDK=/opt/pdks/interposer
CONTAINER_IHP_PDK=/opt/pdks/ihp-sg13g2

# Build the -v mount flags (only for PDK dirs that exist on the host)
PDK_MOUNTS=()
if [ -d "$HOST_INTERCONNECT_PDK" ]; then
    PDK_MOUNTS+=( -v "${HOST_INTERCONNECT_PDK}:${CONTAINER_INTERCONNECT_PDK}:ro" )
else
    echo "warning: interconnect PDK not found at $HOST_INTERCONNECT_PDK" >&2
    echo "         CuPillar/bump bodies will not render (set HOST_INTERCONNECT_PDK)." >&2
fi
[ -d "$HOST_INTERPOSER_PDK" ] && PDK_MOUNTS+=( -v "${HOST_INTERPOSER_PDK}:${CONTAINER_INTERPOSER_PDK}:ro" )
[ -d "$HOST_IHP_PDK" ]        && PDK_MOUNTS+=( -v "${HOST_IHP_PDK}:${CONTAINER_IHP_PDK}:ro" )
