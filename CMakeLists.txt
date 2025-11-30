cmake_minimum_required(VERSION 3.16)
project(chiplet-studio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Core5Compat)

# Find yaml-cpp
find_package(yaml-cpp REQUIRED)

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
    src/core/Assembly.cpp
    src/core/Component.cpp
    src/core/Interface.cpp
    src/core/Technology.cpp
)

set(FORMAT_SOURCES
    src/formats/ChipletFormat.cpp
)

set(VIEW3D_MATH_SOURCES
    src/view3d/math/VECTOR3D.cpp
    src/view3d/math/VECTOR4D.cpp
    src/view3d/math/MATRIX4X4.cpp
    src/view3d/math/PLANE.cpp
    src/view3d/math/FRUSTUM.cpp
    src/view3d/math/AA_BOUNDING_BOX.cpp
)

set(VIEW3D_SOURCES
    src/view3d/AssemblyRenderer.cpp
    src/view3d/SceneManager.cpp
    src/view3d/Camera.cpp
    src/view3d/ShaderProgram.cpp
    src/view3d/ComponentMesh.cpp
    src/view3d/MeshBuilder.cpp
    src/view3d/AssemblyView.cpp
    src/view3d/ClipPlane.cpp
    ${VIEW3D_MATH_SOURCES}
)

set(VIEW2D_SOURCES
    src/view2d/KLayoutBridge.cpp
    src/view2d/LayerProperties.cpp
    src/view2d/KLayout2DView.cpp
)

set(UI_SOURCES
    src/ui/MainWindow.cpp
    src/ui/HierarchyPanel.cpp
    src/ui/PropertiesPanel.cpp
    src/ui/ComponentStyle.cpp
    src/ui/UnitConverter.cpp
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
    ${FORMAT_SOURCES}
    ${VIEW3D_SOURCES}
    ${VIEW2D_SOURCES}
    ${UI_SOURCES}
)

# Embed shader files as Qt resources
qt_add_resources(chiplet-studio "shaders"
    PREFIX "/shaders"
    FILES ${SHADER_SOURCES}
)

target_link_libraries(chiplet-studio
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Core5Compat
    yaml-cpp
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

# Tests
enable_testing()
add_subdirectory(tests)
