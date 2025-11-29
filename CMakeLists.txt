cmake_minimum_required(VERSION 3.16)
project(chiplet-studio VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets)

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
    ${KLAYOUT_SRC_DIR}
    ${KLAYOUT_SRC_DIR}/db/db
    ${KLAYOUT_SRC_DIR}/tl/tl
    ${KLAYOUT_SRC_DIR}/gsi/gsi
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

set(VIEW3D_SOURCES
    src/view3d/AssemblyRenderer.cpp
    src/view3d/SceneManager.cpp
)

set(VIEW2D_SOURCES
    src/view2d/KLayoutBridge.cpp
)

set(UI_SOURCES
    src/ui/MainWindow.cpp
    src/ui/HierarchyPanel.cpp
    src/ui/PropertiesPanel.cpp
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

target_link_libraries(chiplet-studio
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    yaml-cpp
)

# Link KLayout libraries if available
if(HAVE_KLAYOUT)
    target_compile_definitions(chiplet-studio PRIVATE HAVE_KLAYOUT)
    target_link_directories(chiplet-studio PRIVATE ${KLAYOUT_BUILD_DIR})
    target_link_libraries(chiplet-studio
        klayout_tl
        klayout_gsi
        klayout_db
    )
    # Set RPATH so runtime can find KLayout libraries
    set_target_properties(chiplet-studio PROPERTIES
        BUILD_RPATH "${KLAYOUT_BUILD_DIR}"
        INSTALL_RPATH "${KLAYOUT_BUILD_DIR}"
    )
    message(STATUS "KLayout integration: ENABLED")
else()
    message(STATUS "KLayout integration: DISABLED (stub implementation)")
endif()

# Tests
enable_testing()
add_subdirectory(tests)
