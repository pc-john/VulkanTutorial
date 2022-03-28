
# find paths and libraries
find_path(Wayland_client_INCLUDE_DIR NAMES wayland-client.h)
find_path(Wayland_server_INCLUDE_DIR NAMES wayland-server.h)
find_library(Wayland_client_LIBRARY  NAMES wayland-client)
find_library(Wayland_server_LIBRARY  NAMES wayland-server)
find_program(Wayland_SCANNER         NAMES wayland-scanner)
find_path(Wayland_PROTOCOLS_DIR      NAMES stable/xdg-shell/xdg-shell.xml
                                     PATH_SUFFIXES share/wayland-protocols)

# Wayland_INCLUDE_DIR
set(Wayland_INCLUDE_DIR ${Wayland_client_INCLUDE_DIR} ${Wayland_server_INCLUDE_DIR})
list(REMOVE_DUPLICATES Wayland_INCLUDE_DIR)

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
