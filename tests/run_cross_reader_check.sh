#!/bin/bash
# run_cross_reader_check.sh - drive this studio's C++ writer output through PyYAML.
#
# Registered in ctest UNCONDITIONALLY and on purpose. The obvious alternative,
# guarding the ctest entry on the venv existing, is what chiplet_python_bindings
# does with if(HAVE_PYTHON): on an image without the dependency the test simply
# ceases to exist, the gate stays green, and the only trace is a smaller test
# count that nobody reads. Every failure mode here is a red test with a message
# naming what is missing and how to get it.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# The venv is built into docker/Dockerfile.build at this path. It is a TEST
# dependency: PyYAML is never a Studio runtime dependency, is not in
# Dockerfile.runtime, and is never installed outside this venv.
VENV_PYTHON="${CROSS_READER_PYTHON:-/opt/crossreader-venv/bin/python}"

if [ ! -x "$VENV_PYTHON" ]; then
    echo "FAIL: no cross-reader interpreter at ${VENV_PYTHON}." >&2
    echo "" >&2
    echo "This check is not optional and does not skip. It is the only thing in" >&2
    echo "this repository that reads the writer's output with anything other than" >&2
    echo "the writer's own YAML implementation." >&2
    echo "" >&2
    echo "The image is stale. Rebuild it:  ./scripts/build-docker.sh" >&2
    echo "Or point CROSS_READER_PYTHON at an interpreter with PyYAML installed." >&2
    exit 1
fi

# The producer half. ctest sets CHIPLET_PYTHON_MODULE_DIR to the real binary dir.
export PYTHONPATH="${CHIPLET_PYTHON_MODULE_DIR:-${PROJECT_DIR}/build/python}:${PYTHONPATH:-}"

exec "$VENV_PYTHON" "${SCRIPT_DIR}/cross_reader_check.py"
