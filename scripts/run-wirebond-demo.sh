#!/bin/bash
# run-wirebond-demo.sh - Launch Chiplet Studio with the two-die interposer
# example preloaded (adk-tools examples/two_die_interposer, formerly
# interposer_wire_bonding_demo, which is where this script's name comes from).
#
# This is a compatibility name, not a second launcher. run-chiplet-demo.sh
# already defaults to that example, and the two scripts had
# drifted into byte-identical bodies whose only differences were the header
# sentence and the usage line. Two copies of a launch invocation is two places
# for an environment variable to be missing from, which is exactly how the
# embedded interpreter ended up without PYTHONPATH in both of them, so this
# delegates rather than repeating it.
#
# Arguments and environment are forwarded unchanged: exec replaces this process,
# so the exit status is the canonical script's own and nothing is remapped.
#
# Usage: ./scripts/run-wirebond-demo.sh [path/to/file.chiplet]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/run-chiplet-demo.sh" "$@"
