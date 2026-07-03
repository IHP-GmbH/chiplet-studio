cmake_minimum_required(VERSION 3.16)
project(chiplet-studio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Debug build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()
set(CMAKE_CXX_FLAGS_DEBUG "-g3 -O0 -DDEBUG_ENABLED" CACHE STRING "Debug flags" FORCE)
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-g -O2 -DNDEBUG" CACHE STRING "RelWithDebInfo flags" FORCE)

# Include sanitizers configuration (ASan, UBSan, TSan)
include(${CMAKE_SOURCE_DIR}/cmake/Sanitizers.cmake)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Core5Compat Concurrent)

# Find yaml-cpp
find_package(yaml-cpp REQUIRED)

# Fetch nlohmann_json for Command serialization/journaling
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Python scripting support (optional)
option(ENABLE_PYTHON_SCRIPTING "Enable Python scripting support" ON)
set(HAVE_PYTHON FALSE)

if(ENABLE_PYTHON_SCRIPTING)
    find_package(Python3 COMPONENTS Interpreter Development)
    if(Python3_FOUND)
        message(STATUS "Python ${Python3_VERSION} found")
        message(STATUS "  Interpreter: ${Python3_EXECUTABLE}")
        message(STATUS "  Include dirs: ${Python3_INCLUDE_DIRS}")
        message(STATUS "  Libraries: ${Python3_LIBRARIES}")

        # Fetch pybind11
        FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.11.1
        )
        FetchContent_MakeAvailable(pybind11)
        set(HAVE_PYTHON TRUE)
        message(STATUS "Python scripting: ENABLED")
    else()
        message(WARNING "Python development libraries not found. Python scripting disabled.")
        message(STATUS "Python scripting: DISABLED")
    endif()
else()
    message(STATUS "Python scripting: DISABLED (by option)")
endif()

# KLayout integration
# KLayout must be built separately using its build.sh script
# Set KLAYOUT_BUILD_DIR to point to the KLayout build directory
set(KLAYOUT_SRC_DIR "${CMAKE_SOURCE_DIR}/extern/klayout/src")
set(KLAYOUT_BUILD_DIR "" CACHE PATH "Path to KLayout build directory (e.g., extern/klayout/bin-release)")

# Check if KLayout libraries exist
set(HAVE_KLAYOUT FALSE)
if(KLAYOUT_BUILD_DIR)
    if(EXISTS "${KLAYOUT_BUILD_DIR}/libklayout_db.so" OR
       EXISTS "${KLAYOUT_BUILD_DIR}/libklayout_db.dylib" OR
       EXISTS "${KLAYOUT_BUILD_DIR}/klayout_db.dll")
        set(HAVE_KLAYOUT TRUE)
        message(STATUS "KLayout libraries found at ${KLAYOUT_BUILD_DIR}")
    else()
        message(WARNING "KLAYOUT_BUILD_DIR set but libraries not found. Run KLayout build first.")
    endif()
else()
    message(WARNING "KLAYOUT_BUILD_DIR not set. KLayout integration will be disabled.")
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    # Vendored Apache-2.0 .chiplet reference reader/writer (see
    # src/formats/chiplet_format_io/VENDORED.md). Lets the host #include
    # <chiplet_format_io/chiplet_format_io.hpp>.
    ${CMAKE_SOURCE_DIR}/src/formats/chiplet_format_io/include
    ${CMAKE_SOURCE_DIR}/src/view3d
    ${KLAYOUT_SRC_DIR}
    ${KLAYOUT_SRC_DIR}/db/db
    ${KLAYOUT_SRC_DIR}/tl/tl
    ${KLAYOUT_SRC_DIR}/gsi/gsi
    # KLayout 2D view libraries
    ${KLAYOUT_SRC_DIR}/layview/layview
    ${KLAYOUT_SRC_DIR}/laybasic/laybasic
    ${KLAYOUT_SRC_DIR}/lay/lay
    ${KLAYOUT_SRC_DIR}/layui/layui
    ${KLAYOUT_SRC_DIR}/ant/ant
    ${KLAYOUT_SRC_DIR}/img/img
    ${KLAYOUT_SRC_DIR}/edt/edt
    ${KLAYOUT_SRC_DIR}/rdb/rdb
)

# Source files
set(CORE_SOURCES
    src/core/Logger.cpp
    src/core/Assembly.cpp
    src/core/Component.cpp
    src/core/Interface.cpp
    src/core/IOPad.cpp
    src/core/Netlist.cpp
    src/core/Technology.cpp
    src/core/LayerStackup.cpp
    src/core/CommandProcessor.cpp
    src/core/CommandJournal.cpp
    src/core/CommandFactory.cpp
    src/core/commands/CmdMoveComponent.cpp
    src/core/commands/CmdRenameComponent.cpp
    src/core/commands/CmdSetRenderMode.cpp
)

set(FLOW_SOURCES
    src/core/flow/FlowEngine.cpp
    src/core/flow/FlowConfig.cpp
)

set(FORMAT_SOURCES
    src/formats/ChipletFormat.cpp
    src/formats/ChipletSynth.cpp
    src/formats/chiplet_format_io/src/chiplet_format_io.cpp
)

set(VIEW3D_MATH_SOURCES
    src/view3d/math/VECTOR3D.cpp
    src/view3d/math/VECTOR4D.cpp
    src/view3d/math/MATRIX4X4.cpp
    src/view3d/math/PLANE.cpp
    src/view3d/math/FRUSTUM.cpp
    src/view3d/math/AA_BOUNDING_BOX.cpp
    src/view3d/math/BVH.cpp
)

set(VIEW3D_SOURCES
    src/view3d/SceneManager.cpp
    src/view3d/Camera.cpp
    src/view3d/ShaderProgram.cpp
    src/view3d/ComponentMesh.cpp
    src/view3d/MeshBuilder.cpp
    src/view3d/AssemblyView.cpp
    src/view3d/GDSLayerExtractor.cpp
    src/view3d/GDSAnalyzer.cpp
    src/view3d/LayerMeshBuilder.cpp
    src/view3d/ShapeFilter.cpp
    ${VIEW3D_MATH_SOURCES}
)

set(VIEW2D_SOURCES
    src/view2d/KLayoutBridge.cpp
    src/view2d/LayerProperties.cpp
    src/view2d/KLayout2DView.cpp
    src/view2d/EmbeddedDispatcher.cpp
    src/view2d/CellComponentMapper.cpp
    src/view2d/OverviewNavigator.cpp
)

set(UI_SOURCES
    src/ui/MainWindow.cpp
    src/ui/HierarchyPanel.cpp
    src/ui/PropertiesPanel.cpp
    src/ui/DrillDownPanel.cpp
    src/ui/ComponentStyle.cpp
    src/ui/UnitConverter.cpp
    src/ui/CrashRecoveryDialog.cpp
    src/ui/CellSelectionDialog.cpp
    src/ui/ImportGdsDialog.cpp
    src/ui/ExportPngDialog.cpp
    src/ui/ScriptConsole.cpp
    src/ui/FlowPanel.cpp
    src/ui/NetGraphPanel.cpp
)

# Scripting sources (always included, conditionally compiled)
set(SCRIPTING_SOURCES
    src/scripting/ScriptEngine.cpp
)

# Shader resources
set(SHADER_SOURCES
    src/view3d/shaders/component.vert
    src/view3d/shaders/component.frag
    src/view3d/shaders/grid.vert
    src/view3d/shaders/grid.frag
)

# Main executable
add_executable(chiplet-studio
    src/main.cpp
    ${CORE_SOURCES}
    ${FLOW_SOURCES}
    ${FORMAT_SOURCES}
    ${VIEW3D_SOURCES}
    ${VIEW2D_SOURCES}
    ${UI_SOURCES}
    ${SCRIPTING_SOURCES}
)

# Embed shader files as Qt resources
qt_add_resources(chiplet-studio "shaders"
    PREFIX "/shaders"
    FILES ${SHADER_SOURCES}
)

# Embed logo and other images
qt_add_resources(chiplet-studio "images"
    PREFIX "/images"
    FILES resources/ihp_logo.png
)

target_link_libraries(chiplet-studio
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Core5Compat
    Qt6::Concurrent
    yaml-cpp
    nlohmann_json::nlohmann_json
    chiplet_gds3d
)

# Link KLayout libraries if available
if(HAVE_KLAYOUT)
    target_compile_definitions(chiplet-studio PRIVATE HAVE_KLAYOUT HAVE_QT)
    target_link_directories(chiplet-studio PRIVATE
        ${KLAYOUT_BUILD_DIR}
        ${KLAYOUT_BUILD_DIR}/db_plugins
    )
    # Force loading of format plugins (they register via static constructors)
    target_link_libraries(chiplet-studio
        # Core libraries
        klayout_tl
        klayout_gsi
        klayout_db
        # 2D view libraries
        klayout_laybasic
        klayout_lay
        klayout_layui
        klayout_layview
        klayout_ant
        klayout_img
        klayout_edt
        klayout_rdb
        # Format plugins
        -Wl,--no-as-needed  # Prevent linker from dropping unused libraries
        gds2  # Format plugin for GDS files
        -Wl,--as-needed
    )
    # Set RPATH so runtime can find KLayout libraries and plugins
    set_target_properties(chiplet-studio PROPERTIES
        BUILD_RPATH "${KLAYOUT_BUILD_DIR};${KLAYOUT_BUILD_DIR}/db_plugins"
        INSTALL_RPATH "${KLAYOUT_BUILD_DIR};${KLAYOUT_BUILD_DIR}/db_plugins"
    )
    message(STATUS "KLayout integration: ENABLED (with 2D view)")
else()
    message(STATUS "KLayout integration: DISABLED (stub implementation)")
endif()

# BlenderGDS configs directory
target_compile_definitions(chiplet-studio PRIVATE
    CONFIGS_DIR="${CMAKE_SOURCE_DIR}/configs")

# Link Python for embedded interpreter in main executable
if(HAVE_PYTHON)
    target_compile_definitions(chiplet-studio PRIVATE HAVE_PYTHON
        PYTHON_MODULE_DIR="${CMAKE_BINARY_DIR}/python")
    target_include_directories(chiplet-studio PRIVATE ${pybind11_INCLUDE_DIRS})
    target_link_libraries(chiplet-studio pybind11::embed)
endif()

# Python scripting module
if(HAVE_PYTHON)
    # Create Python module 'chiplet_studio'
    pybind11_add_module(chiplet_studio_py
        src/scripting/PyBindings.cpp
        ${CORE_SOURCES}
        ${FLOW_SOURCES}
        ${FORMAT_SOURCES}
    )

    target_include_directories(chiplet_studio_py PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/extern/GDS3D/libgdsto3d
        ${CMAKE_SOURCE_DIR}/extern/GDS3D/math
        ${KLAYOUT_SRC_DIR}
        ${KLAYOUT_SRC_DIR}/db/db
        ${KLAYOUT_SRC_DIR}/tl/tl
        ${KLAYOUT_SRC_DIR}/gsi/gsi
    )

    target_link_libraries(chiplet_studio_py PRIVATE
        yaml-cpp
        nlohmann_json::nlohmann_json
        Qt6::Core
        chiplet_gds3d
    )

    target_compile_definitions(chiplet_studio_py PRIVATE HAVE_PYTHON CHIPLET_STANDALONE_PYMODULE)

    # Output as 'chiplet_studio.so' in build directory
    set_target_properties(chiplet_studio_py PROPERTIES
        OUTPUT_NAME "chiplet_studio"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/python"
    )

    message(STATUS "Python module will be built at: ${CMAKE_BINARY_DIR}/python/chiplet_studio*.so")
endif()

# GDS3D library for advanced 3D rendering.
# Defined inline rather than via add_subdirectory(extern/GDS3D/libgdsto3d) so the
# extern/GDS3D submodule stays pristine upstream: upstream trilomix/GDS3D ships no
# libgdsto3d/CMakeLists.txt, so keeping this build glue in-repo lets a fresh
# `git clone --recurse-submodules` build without a manual patch step.
set(GDS3D_DIR ${CMAKE_SOURCE_DIR}/extern/GDS3D)
add_library(chiplet_gds3d STATIC
    # Core parsing and geometry sources
    ${GDS3D_DIR}/libgdsto3d/gdsparse.cpp
    ${GDS3D_DIR}/libgdsto3d/gdspolygon.cpp
    ${GDS3D_DIR}/libgdsto3d/process_cfg.cpp
    ${GDS3D_DIR}/libgdsto3d/gdsobject.cpp
    ${GDS3D_DIR}/libgdsto3d/gdsobjectlist.cpp
    ${GDS3D_DIR}/libgdsto3d/gdselements.cpp
    ${GDS3D_DIR}/libgdsto3d/gds_globals.cpp
    ${GDS3D_DIR}/libgdsto3d/gdspath.cpp
    ${GDS3D_DIR}/libgdsto3d/gdstext.cpp
    ${GDS3D_DIR}/libgdsto3d/assembly_cfg.cpp
    # Clipper library for polygon operations
    ${GDS3D_DIR}/libgdsto3d/clipper/clipper.cpp
    # Math library (parent math/ directory)
    ${GDS3D_DIR}/math/AA_BOUNDING_BOX.cpp
    ${GDS3D_DIR}/math/PLANE.cpp
    ${GDS3D_DIR}/math/MATRIX4X4.cpp
    ${GDS3D_DIR}/math/FRUSTUM.cpp
    ${GDS3D_DIR}/math/VECTOR3D.cpp
    ${GDS3D_DIR}/math/VECTOR4D.cpp
)
# PIC is required so the static lib can be linked into the Python module.
set_target_properties(chiplet_gds3d PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_STANDARD 17
)
# Suppress legacy third-party warnings.
target_compile_options(chiplet_gds3d PRIVATE -w)
target_include_directories(chiplet_gds3d PUBLIC
    ${GDS3D_DIR}/libgdsto3d
    ${GDS3D_DIR}/math
    ${GDS3D_DIR}/libgdsto3d/clipper
)

# Tests
enable_testing()
add_subdirectory(tests)
