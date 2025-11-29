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

# Test executable
add_executable(chiplet_tests
    test_chiplet_format.cpp
    test_validation.cpp
    test_klayout_bridge.cpp
)

target_include_directories(chiplet_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

# Base libraries
target_link_libraries(chiplet_tests
    GTest::gtest_main
    yaml-cpp
)

# Link core sources (we need them for testing)
target_sources(chiplet_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core/Assembly.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Component.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Interface.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Technology.cpp
    ${CMAKE_SOURCE_DIR}/src/formats/ChipletFormat.cpp
    ${CMAKE_SOURCE_DIR}/src/view2d/KLayoutBridge.cpp
)

# KLayout integration for tests (if available)
if(HAVE_KLAYOUT)
    target_compile_definitions(chiplet_tests PRIVATE HAVE_KLAYOUT)
    target_include_directories(chiplet_tests PRIVATE
        ${KLAYOUT_SRC_DIR}
        ${KLAYOUT_SRC_DIR}/db/db
        ${KLAYOUT_SRC_DIR}/tl/tl
        ${KLAYOUT_SRC_DIR}/gsi/gsi
    )
    target_link_directories(chiplet_tests PRIVATE ${KLAYOUT_BUILD_DIR})
    target_link_libraries(chiplet_tests
        klayout_tl
        klayout_gsi
        klayout_db
    )
    # Set RPATH so runtime can find KLayout libraries
    set_target_properties(chiplet_tests PROPERTIES
        BUILD_RPATH "${KLAYOUT_BUILD_DIR}"
        INSTALL_RPATH "${KLAYOUT_BUILD_DIR}"
    )
endif()

# Copy test fixtures to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Register tests with CTest
include(GoogleTest)
# Use PRE_TEST discovery mode to avoid running binary at configure time
# (needed because KLayout libs are found via RPATH at runtime)
gtest_discover_tests(chiplet_tests DISCOVERY_MODE PRE_TEST)
