#
# find Wayland
#
# targets:
#    Wayland::client
#
# variables:
#    Wayland_client_FOUND
#    Wayland_INCLUDE_DIR
#    Wayland_client_INCLUDE_DIR
#    Wayland_server_INCLUDE_DIR
#    Wayland_client_LIBRARY
#    Wayland_server_LIBRARY
#    Wayland_SCANNER
#    Wayland_PROTOCOLS_DIR
#


# find paths
find_path(Wayland_client_INCLUDE_DIR NAMES wayland-client.h)
find_path(Wayland_server_INCLUDE_DIR NAMES wayland-server.h)
find_library(Wayland_client_LIBRARY  NAMES wayland-client)
find_library(Wayland_server_LIBRARY  NAMES wayland-server)
find_program(Wayland_SCANNER         NAMES wayland-scanner)

# Wayland include dir
set(Wayland_INCLUDE_DIR ${Wayland_client_INCLUDE_DIR} ${Wayland_server_INCLUDE_DIR})
list(REMOVE_DUPLICATES Wayland_INCLUDE_DIR)

# Wayland protocols directory
find_package(PkgConfig QUIET)
pkg_check_modules(Wayland_PROTOCOLS wayland-protocols QUIET)
if(PKG_CONFIG_FOUND AND Wayland_PROTOCOLS_FOUND)
	pkg_get_variable(Wayland_PROTOCOLS_DIR wayland-protocols pkgdatadir)
endif()
set(Wayland_PROTOCOLS_DIR "${Wayland_PROTOCOLS_DIR}" CACHE PATH "Wayland protocols directory.")

# Wayland::client target
if(Wayland_client_INCLUDE_DIR AND Wayland_client_LIBRARY)
	set(Wayland_client_FOUND TRUE)
	if(NOT TARGET Wayland::client)
		add_library(Wayland::client UNKNOWN IMPORTED)
		set_target_properties(Wayland::client PROPERTIES
			IMPORTED_LOCATION "${Wayland_client_LIBRARY}"
			INTERFACE_INCLUDE_DIRECTORIES "${Wayland_client_INCLUDE_DIR}")
	endif()
endif()
