# Sanitizers.cmake - Configuration for AddressSanitizer and UndefinedBehaviorSanitizer
#
# Usage:
#   cmake -B build -DENABLE_ASAN=ON -DENABLE_UBSAN=ON
#   cmake --build build
#
# Run with:
#   ASAN_OPTIONS=detect_leaks=1:halt_on_error=0 ./build/chiplet-studio

option(ENABLE_ASAN "Enable AddressSanitizer for memory error detection" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Enable ThreadSanitizer for data race detection" OFF)

# Check compiler support
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(SANITIZER_SUPPORTED TRUE)
else()
    set(SANITIZER_SUPPORTED FALSE)
    if(ENABLE_ASAN OR ENABLE_UBSAN OR ENABLE_TSAN)
        message(WARNING "Sanitizers require GCC or Clang compiler")
    endif()
endif()

# AddressSanitizer
if(ENABLE_ASAN AND SANITIZER_SUPPORTED)
    message(STATUS "AddressSanitizer enabled")
    add_compile_options(
        -fsanitize=address
        -fno-omit-frame-pointer
        -fno-optimize-sibling-calls
        -g
    )
    add_link_options(-fsanitize=address)

    # Define for conditional code
    add_compile_definitions(ASAN_ENABLED)
endif()

# UndefinedBehaviorSanitizer
if(ENABLE_UBSAN AND SANITIZER_SUPPORTED)
    message(STATUS "UndefinedBehaviorSanitizer enabled")
    add_compile_options(
        -fsanitize=undefined
        -fno-omit-frame-pointer
        -fno-sanitize=vptr  # Qt uses non-standard vtable handling
        -g
    )
    add_link_options(-fsanitize=undefined)

    add_compile_definitions(UBSAN_ENABLED)
endif()

# ThreadSanitizer (mutually exclusive with ASan)
if(ENABLE_TSAN AND SANITIZER_SUPPORTED)
    if(ENABLE_ASAN)
        message(FATAL_ERROR "ThreadSanitizer cannot be used with AddressSanitizer")
    endif()
    message(STATUS "ThreadSanitizer enabled")
    add_compile_options(
        -fsanitize=thread
        -fno-omit-frame-pointer
        -g
    )
    add_link_options(-fsanitize=thread)

    add_compile_definitions(TSAN_ENABLED)
endif()

# Print summary
if(ENABLE_ASAN OR ENABLE_UBSAN OR ENABLE_TSAN)
    message(STATUS "")
    message(STATUS "=== Sanitizer Configuration ===")
    message(STATUS "  ASan:  ${ENABLE_ASAN}")
    message(STATUS "  UBSan: ${ENABLE_UBSAN}")
    message(STATUS "  TSan:  ${ENABLE_TSAN}")
    message(STATUS "")
    message(STATUS "Run with environment variables:")
    message(STATUS "  ASAN_OPTIONS=detect_leaks=1:halt_on_error=0:print_stacktrace=1")
    message(STATUS "  UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0")
    message(STATUS "================================")
    message(STATUS "")
endif()
