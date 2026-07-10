#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run OpenROAD's check_3dblox on an exported 3Dblox assembly inside a
# locally built OpenROAD image (see README.md). Exits non-zero if the
# linter reports any warning or error, so a flow step turns red on
# geometry findings, not just on crashes.
set -euo pipefail

OUT_DIR=$(cd "${1:?usage: run_check_3dblox.sh <out-dir> <name>}" && pwd)
NAME=${2:?usage: run_check_3dblox.sh <out-dir> <name>}
IMAGE=${OPENROAD_3DBLOX_IMAGE:-openroad-3dblox:ad9e7248}

cat > "$OUT_DIR/check_3dblox.tcl" <<EOF
read_3dbx $NAME.3dbx
puts "READ_3DBX_OK"
check_3dblox
puts "CHECK_3DBLOX_OK"
exit
EOF

# The image's entrypoint is the openroad binary and it runs as an
# unprivileged user, so mount with the host uid to keep the exported
# files readable inside the container.
log=$(docker run --rm --user "$(id -u):$(id -g)" \
    -v "$OUT_DIR:$OUT_DIR" -w "$OUT_DIR" \
    "$IMAGE" -exit "$OUT_DIR/check_3dblox.tcl" 2>&1)
echo "$log"

if printf '%s' "$log" | grep -qE '\[(WARNING|ERROR)'; then
    echo "check_3dblox reported findings (see log above)" >&2
    exit 1
fi
printf '%s' "$log" | grep -q "CHECK_3DBLOX_OK"
