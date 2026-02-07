#!/bin/bash

xhost +local:docker
  docker run --rm \
      -e DISPLAY="$DISPLAY" \
      -e LD_LIBRARY_PATH=/workspace/extern/klayout/bin-release:/workspace/extern/klayout/bin-release/db_plugins \
      -v /tmp/.X11-unix:/tmp/.X11-unix:rw \
      -v ${HOME}/git/heterogenic_chip_design_project/chiplet-studio:/workspace \
      --network host \
      chiplet-studio-build \
      /workspace/build/chiplet-studio

