

find_path(Wayland_client_INCLUDE_DIR NAMES wayland-client.h)
find_path(Wayland_server_INCLUDE_DIR NAMES wayland-server.h)
find_library(Wayland_client_LIBRARY  NAMES wayland-client)
find_library(Wayland_server_LIBRARY  NAMES wayland-server)
find_program(Wayland_SCANNER         NAMES wayland-scanner)

set(Wayland_INCLUDE_DIR ${Wayland_client_INCLUDE_DIR} ${Wayland_server_INCLUDE_DIR})
list(REMOVE_DUPLICATES Wayland_INCLUDE_DIR)
#set(WAYLAND_LIBRARIES ${WAYLAND_CLIENT_LIBRARY} ${WAYLAND_SERVER_LIBRARIES})

#include(FindPackageHandleStandardArgs)
#FIND_PACKAGE_HANDLE_STANDARD_ARGS(Wayland_client  DEFAULT_MSG  Wayland_client_LIBRARY  Wayland_client_INCLUDE_DIR)
#FIND_PACKAGE_HANDLE_STANDARD_ARGS(WAYLAND_SERVER  DEFAULT_MSG  WAYLAND_SERVER_LIBRARY  WAYLAND_SERVER_INCLUDE_DIR)
#FIND_PACKAGE_HANDLE_STANDARD_ARGS(WAYLAND         DEFAULT_MSG  WAYLAND_LIBRARIES         WAYLAND_INCLUDE_DIR)
#FIND_PACKAGE_HANDLE_STANDARD_ARGS(WAYLAND_SCANNER DEFAULT_MSG  WAYLAND_SCANNER)

find_package(PkgConfig QUIET)
pkg_check_modules(Wayland_PROTOCOLS wayland-protocols QUIET)
if(wayland-protocols_FOUND)
    pkg_get_variable(Wayland_PROTOCOLS_DIR wayland-protocols pkgdatadir)
endif()
set(Wayland_PROTOCOLS_DIR "${Wayland_PROTOCOLS_DIR}" CACHE PATH "Wayland protocols directory.")

if(Wayland_client_INCLUDE_DIR AND Wayland_client_LIBRARY)
    set(Wayland_client_FOUND TRUE)
    if(NOT TARGET Wayland::client)
        add_library(Wayland::client UNKNOWN IMPORTED)
        set_target_properties(Wayland::client PROPERTIES
            IMPORTED_LOCATION "${Wayland_client_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Wayland_client_INCLUDE_DIR}")
    endif()
endif()
