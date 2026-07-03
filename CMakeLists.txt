cmake_minimum_required(VERSION 3.15)
project(opengnm C)

# opengnm is a clean-rewrite PS4 GNM library providing sceGnm*/sceGpa*
# drop-in API compatibility with the Sony official PS4 SDK.
#
# Build configuration:
#   OPENGNM_PLATFORM  — auto (default), orbis, or generic
#   OPENGNM_TOOLS     — OFF (default), build CLI tools (separate repo normally)

set(OPENGNM_PLATFORM "auto" CACHE STRING "opengnm platform: auto, orbis, or generic")
set_property(CACHE OPENGNM_PLATFORM PROPERTY STRINGS auto orbis generic)

# Auto-detect platform
if(OPENGNM_PLATFORM STREQUAL "orbis")
    set(OPENGNM_ORBIS_PLATFORM ON)
elseif(OPENGNM_PLATFORM STREQUAL "generic")
    set(OPENGNM_ORBIS_PLATFORM OFF)
else()
    # auto-detect
    if(ORBIS OR PLATFORM_PS4 OR CMAKE_SYSTEM_NAME STREQUAL "OpenOrbis")
        set(OPENGNM_ORBIS_PLATFORM ON)
    else()
        set(OPENGNM_ORBIS_PLATFORM OFF)
    endif()
endif()

# Source files (will be populated as implementation is added)
set(OPENGNM_SOURCES
    # Phase 2 — core implementation (to be added)
    # src/drawcommandbuffer.c
    # src/rendertarget.c
    # src/depthrendertarget.c
    # src/texture.c
    # src/shader.c
    # src/dataformat.c
    # src/commandbuffer.c
    # src/error.c
)

# Orbis-specific sources
if(OPENGNM_ORBIS_PLATFORM)
    list(APPEND OPENGNM_SOURCES
        # src/driver_orbis.c
        # src/platform_orbis.c
    )
else()
    list(APPEND OPENGNM_SOURCES
        # src/driver_generic.c
        # src/platform_generic.c
    )
endif()

# GpuAddr sources (ported from freegnm, AMD PAL-derived math)
list(APPEND OPENGNM_SOURCES
    # src/gpuaddr/surface.c
    # src/gpuaddr/tilemodes.c
    # src/gpuaddr/tiler.c
    # src/gpuaddr/decompress.c
    # src/gpuaddr/surfgen.c
)

# GCN assembler (internal, needed for fetch shader generation)
list(APPEND OPENGNM_SOURCES
    # src/gcn/assembler.c
    # src/gcn/decoder.c
)

# PM4 encoding
list(APPEND OPENGNM_SOURCES
    # src/pm4/format.c
    # src/pm4/decoder.c
)

# New functionality
list(APPEND OPENGNM_SOURCES
    # src/validate.c
    # src/resource.c
    # src/workload.c
    # src/debugprof.c
)

# Only build the library if we have source files
if(OPENGNM_SOURCES)
    add_library(opengnm STATIC ${OPENGNM_SOURCES})

    target_include_directories(opengnm PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    # Suppress warnings in the C library
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(opengnm PRIVATE -w)
    endif()

    if(OPENGNM_ORBIS_PLATFORM)
        target_compile_definitions(opengnm PUBLIC OPENGNM_ORBIS)
        target_link_libraries(opengnm PUBLIC kernel SceGnmDriver)
    else()
        target_compile_definitions(opengnm PUBLIC OPENGNM_GENERIC)
    endif()
else()
    message(STATUS "opengnm: No source files configured yet (Phase 1 — headers only)")
endif()

# === Tests ===
option(OPENGNM_BUILD_TESTS "Build opengnm tests" ON)
if(OPENGNM_BUILD_TESTS)
    enable_testing()
    # Tests will be added in Phase 7
    # add_subdirectory(tests)
endif()

# === Install ===
include(GNUInstallDirs)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")
