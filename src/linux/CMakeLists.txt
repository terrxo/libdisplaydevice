# A global identifier for the library
set(MODULE libdisplaydevice_linux)
set(MODULE_ALIAS libdisplaydevice::platform)

# Glob headers and sources
file(GLOB HEADER_LIST CONFIGURE_DEPENDS "include/display_device/linux/*.h")
file(GLOB SOURCE_LIST CONFIGURE_DEPENDS "*.cpp")

# Build the library
add_library(${MODULE} ${HEADER_LIST} ${SOURCE_LIST})
add_library(${MODULE_ALIAS} ALIAS ${MODULE})

# Public includes
target_include_directories(${MODULE} PUBLIC include)

# External libraries
include(Boost_DD)
include(Json_DD)

# Link dependencies.
#  - libevdi: virtual display creation (DisplayLink/evdi upstream, DKMS)
#  - libdrm: enumerate connectors, read connector EDIDs/modes
#  - wayland-client: bind wlr-output-management for topology/mode setting
# All optional during Phase 1; symbols guarded with LIBDD_HAVE_* defines.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBDRM IMPORTED_TARGET libdrm)
    pkg_check_modules(WAYLAND_CLIENT IMPORTED_TARGET wayland-client)
endif()

# libevdi has no .pc file in libevdi-dev; check for the header instead.
find_path(LIBEVDI_INCLUDE_DIR evdi_lib.h)
find_library(LIBEVDI_LIBRARY NAMES evdi)
if(LIBEVDI_INCLUDE_DIR AND LIBEVDI_LIBRARY)
    set(LIBEVDI_FOUND TRUE)
    message(STATUS "Found libevdi: ${LIBEVDI_LIBRARY}")
endif()

target_link_libraries(${MODULE} PRIVATE
        Boost::algorithm
        Boost::scope
        Boost::uuid
        libdisplaydevice::common
        nlohmann_json::nlohmann_json
)

if(LIBEVDI_FOUND)
    target_include_directories(${MODULE} PRIVATE ${LIBEVDI_INCLUDE_DIR})
    target_link_libraries(${MODULE} PRIVATE ${LIBEVDI_LIBRARY})
    target_compile_definitions(${MODULE} PRIVATE LIBDD_HAVE_LIBEVDI=1)
endif()
if(LIBDRM_FOUND)
    target_link_libraries(${MODULE} PRIVATE PkgConfig::LIBDRM)
    target_compile_definitions(${MODULE} PRIVATE LIBDD_HAVE_LIBDRM=1)
endif()
if(WAYLAND_CLIENT_FOUND)
    target_link_libraries(${MODULE} PRIVATE PkgConfig::WAYLAND_CLIENT)
    target_compile_definitions(${MODULE} PRIVATE LIBDD_HAVE_WAYLAND=1)
endif()

# Privileged helper binary (built but NOT installed automatically; install
# with `sudo install -m 4755 -o root -g root apollo-evdi-helper /usr/local/bin/`)
add_executable(apollo-evdi-helper helper/apollo-evdi-helper.c)
set_target_properties(apollo-evdi-helper PROPERTIES C_STANDARD 11)

# Phase 2 standalone smoke test
add_executable(test_evdi_phase2 tools/test_evdi_phase2.cpp)
target_link_libraries(test_evdi_phase2 PRIVATE ${MODULE} libdisplaydevice::common)
if(LIBEVDI_FOUND)
    target_include_directories(test_evdi_phase2 PRIVATE ${LIBEVDI_INCLUDE_DIR})
endif()
