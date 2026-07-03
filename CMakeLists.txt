cmake_minimum_required(VERSION 3.15)
project(opengnm C)

# opengnm — clean-rewrite PS4 GNM library
# sceGnm*/sceGpa* drop-in API compatibility with the Sony official PS4 SDK.

set(OPENGNM_PLATFORM "auto" CACHE STRING "opengnm platform: auto, orbis, or generic")
set_property(CACHE OPENGNM_PLATFORM PROPERTY STRINGS auto orbis generic)

if(OPENGNM_PLATFORM STREQUAL "orbis")
    set(OPENGNM_ORBIS_PLATFORM ON)
elseif(OPENGNM_PLATFORM STREQUAL "generic")
    set(OPENGNM_ORBIS_PLATFORM OFF)
else()
    if(ORBIS OR PLATFORM_PS4 OR CMAKE_SYSTEM_NAME STREQUAL "OpenOrbis")
        set(OPENGNM_ORBIS_PLATFORM ON)
    else()
        set(OPENGNM_ORBIS_PLATFORM OFF)
    endif()
endif()

# Core GNM sources
set(OPENGNM_SOURCES
    src/commandbuffer.c
    src/dataformat.c
    src/depthrendertarget.c
    src/drawcommandbuffer.c
    src/error.c
    src/rendertarget.c
    src/shader.c
    src/texture.c
)

# GCN assembler (internal — needed for fetch shader generation)
set(OPENGNM_GCN_SOURCES
    src/gcn/analyzer.c
    src/gcn/assembler.c
    src/gcn/decoder.c
    src/gcn/error.c
    src/gcn/format.c
    src/gcn/types.c
)

# PM4 encoding (internal)
set(OPENGNM_PM4_SOURCES
    src/pm4/decoder.c
    src/pm4/error.c
    src/pm4/format.c
    src/pm4/types.c
)

# GpuAddr — surface computation (AMD PAL-derived math)
set(OPENGNM_GPUADDR_SOURCES
    src/gpuaddr/decompress.c
    src/gpuaddr/error.c
    src/gpuaddr/surface.c
    src/gpuaddr/surfgen.c
    src/gpuaddr/tilemodes.c
    src/gpuaddr/tiler.c
)

list(APPEND OPENGNM_SOURCES
    ${OPENGNM_GCN_SOURCES}
    ${OPENGNM_PM4_SOURCES}
    ${OPENGNM_GPUADDR_SOURCES}
)

# Platform-specific sources
if(OPENGNM_ORBIS_PLATFORM)
    list(APPEND OPENGNM_SOURCES
        src/driver_orbis.c
        src/platform_orbis.c
    )
else()
    list(APPEND OPENGNM_SOURCES
        src/driver_generic.c
        src/platform_generic.c
    )
endif()

add_library(opengnm STATIC ${OPENGNM_SOURCES})

target_include_directories(opengnm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(opengnm PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(opengnm PRIVATE -w)
endif()

if(OPENGNM_ORBIS_PLATFORM)
    target_compile_definitions(opengnm PUBLIC OPENGNM_ORBIS OPENGNM_REQUIRE_ABI)
    target_link_libraries(opengnm PUBLIC kernel SceGnmDriver)
else()
    target_compile_definitions(opengnm PUBLIC OPENGNM_GENERIC)
endif()

# === Tests ===
option(OPENGNM_BUILD_TESTS "Build opengnm tests" ON)
if(OPENGNM_BUILD_TESTS)
    enable_testing()

    add_executable(opengnm_tests
        tests/test_main.c
        tests/test_surface.c
        tests/test_drawcmd.c
        tests/test_validate.c
        tests/test_api.c
        tests/test_compat.c
    )
    target_link_libraries(opengnm_tests PRIVATE opengnm)
    target_include_directories(opengnm_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    # Tests need to see internal headers (src/u/utility.h etc.)
    target_include_directories(opengnm_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_test(NAME opengnm_all_tests COMMAND opengnm_tests)
endif()

# === Install ===
include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")
install(TARGETS opengnm ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
