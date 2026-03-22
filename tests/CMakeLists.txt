# Tests CMakeLists.txt

include(FetchContent)

# Fetch Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Test executable with custom main for early Python init
add_executable(chiplet_tests
    test_main.cpp  # Custom main with early Python initialization
    test_chiplet_format.cpp
    test_interfaces.cpp
    test_netlist.cpp
    test_validation.cpp
    test_klayout_bridge.cpp
    test_gds_loading.cpp
    test_layer_properties.cpp
    test_clip_plane.cpp
    test_component_style.cpp
    test_hierarchy_panel.cpp
    test_properties_panel.cpp
    test_klayout_2dview.cpp
    test_drilldown_panel.cpp
    test_command_processor.cpp
    test_bvh.cpp
    test_file_loading_crash.cpp
    test_visualization.cpp
    test_async.cpp
    test_gds3d_linkage.cpp
    test_stackup_rendering.cpp
    test_scripting.cpp
    test_tessellation.cpp
    test_integration_final.cpp
    test_layer_stackup_blendergds.cpp
    test_flow_engine.cpp
)

target_include_directories(chiplet_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/view3d
    ${CMAKE_SOURCE_DIR}/extern/GDS3D/libgdsto3d
    ${CMAKE_SOURCE_DIR}/extern/GDS3D/math
)

# Find Qt6 (needed for LayerProperties XML parsing, ClipPlane vectors, HierarchyPanel, and KLayout2DView)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Core5Compat Test Concurrent)

# Base libraries (using GTest::gtest, not gtest_main - we provide our own main for early Python init)
target_link_libraries(chiplet_tests
    GTest::gtest
    yaml-cpp
    nlohmann_json::nlohmann_json
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Core5Compat
    Qt6::Test
    Qt6::Concurrent
    chiplet_gds3d
)

# Link core sources (we need them for testing)
target_sources(chiplet_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/Assembly.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Component.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Interface.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Netlist.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Technology.cpp
    ${CMAKE_SOURCE_DIR}/src/core/LayerStackup.cpp
    ${CMAKE_SOURCE_DIR}/src/core/CommandProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/core/CommandJournal.cpp
    ${CMAKE_SOURCE_DIR}/src/core/CommandFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/core/commands/CmdMoveComponent.cpp
    ${CMAKE_SOURCE_DIR}/src/core/commands/CmdRenameComponent.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/ChipletFormat.cpp
    ${CMAKE_SOURCE_DIR}/src/view2d/KLayoutBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/view2d/LayerProperties.cpp
    ${CMAKE_SOURCE_DIR}/src/view2d/KLayout2DView.cpp
    ${CMAKE_SOURCE_DIR}/src/view2d/EmbeddedDispatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/view3d/ClipPlane.cpp
    ${CMAKE_SOURCE_DIR}/src/view3d/math/VECTOR3D.cpp
    ${CMAKE_SOURCE_DIR}/src/view3d/math/VECTOR4D.cpp
    ${CMAKE_SOURCE_DIR}/src/view3d/math/MATRIX4X4.cpp
    ${CMAKE_SOURCE_DIR}/src/view3d/math/PLANE.cpp
    ${CMAKE_SOURCE_DIR}/src/view3d/math/FRUSTUM.cpp
    ${CMAKE_SOURCE_DIR}/src/view3d/math/AA_BOUNDING_BOX.cpp
    ${CMAKE_SOURCE_DIR}/src/view3d/math/BVH.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/ComponentStyle.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/HierarchyPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/UnitConverter.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/PropertiesPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/DrillDownPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/scripting/ScriptEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/view3d/LayerMeshBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/view3d/GDSLayerExtractor.cpp
    ${CMAKE_SOURCE_DIR}/src/view3d/ComponentMesh.cpp
    ${CMAKE_SOURCE_DIR}/src/core/flow/FlowEngine.cpp
)

# KLayout integration for tests (if available)
if(HAVE_KLAYOUT)
    target_compile_definitions(chiplet_tests PRIVATE HAVE_KLAYOUT HAVE_QT)
    target_include_directories(chiplet_tests PRIVATE
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
    target_link_directories(chiplet_tests PRIVATE
        ${KLAYOUT_BUILD_DIR}
        ${KLAYOUT_BUILD_DIR}/db_plugins
    )
    # Force loading of format plugins (they register via static constructors)
    target_link_libraries(chiplet_tests
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
    set_target_properties(chiplet_tests PROPERTIES
        BUILD_RPATH "${KLAYOUT_BUILD_DIR};${KLAYOUT_BUILD_DIR}/db_plugins"
        INSTALL_RPATH "${KLAYOUT_BUILD_DIR};${KLAYOUT_BUILD_DIR}/db_plugins"
    )
endif()

# Python scripting support for tests (if available)
if(HAVE_PYTHON)
    target_compile_definitions(chiplet_tests PRIVATE HAVE_PYTHON
        PYTHON_MODULE_DIR="${CMAKE_BINARY_DIR}/python")
    target_link_libraries(chiplet_tests pybind11::embed)
endif()

# Fixture path for tests (absolute path, works regardless of CWD)
target_compile_definitions(chiplet_tests PRIVATE
    FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    CONFIGS_DIR="${CMAKE_SOURCE_DIR}/configs")

# Copy test fixtures to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Register tests with CTest
include(GoogleTest)
# Use PRE_TEST discovery mode to avoid running binary at configure time
# (needed because KLayout libs are found via RPATH at runtime)
# Set WORKING_DIRECTORY so tests can find fixtures at relative path
gtest_discover_tests(chiplet_tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
