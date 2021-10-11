set(APP_NAME 11-resizableWindow)

project(${APP_NAME})

set(APP_SOURCES
    main.cpp
    VulkanWindow.cpp
   )

set(APP_INCLUDES
    VulkanWindow.h
   )

# dependencies
set(CMAKE_MODULE_PATH "${${APP_NAME}_SOURCE_DIR}/;${CMAKE_MODULE_PATH}")
find_package(Vulkan REQUIRED)
if(0)
	find_package(Wayland REQUIRED)
	if(Wayland_client_FOUND AND Wayland_SCANNER AND Wayland_PROTOCOLS_DIR)

		add_custom_command(OUTPUT xdg-shell-client-protocol.h
								COMMAND ${Wayland_SCANNER} client-header ${Wayland_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h)
		add_custom_command(OUTPUT xdg-shell-protocol.c
								COMMAND ${Wayland_SCANNER} private-code ${Wayland_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c)
		add_custom_command(OUTPUT xdg-decoration-client-protocol.h
								COMMAND ${Wayland_SCANNER} client-header ${Wayland_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-client-protocol.h)
		add_custom_command(OUTPUT xdg-decoration-protocol.c
								COMMAND ${Wayland_SCANNER} private-code ${Wayland_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-protocol.c)
		list(APPEND APP_SOURCES xdg-shell-protocol.c xdg-decoration-protocol.c)
		list(APPEND APP_INCLUDES xdg-shell-client-protocol.h xdg-decoration-client-protocol.h)
		set(deps Vulkan::Vulkan Wayland::client -lrt)

	endif()
elseif(0)
	find_package(X11 REQUIRED)
	set(deps Vulkan::Vulkan X11)
elseif(1)
	set(deps Vulkan::Vulkan)
endif()

# executable
add_executable(${APP_NAME} ${APP_SOURCES} ${APP_INCLUDES})

# target
target_link_libraries(${APP_NAME} ${deps})
set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)
if(Wayland_client_FOUND AND Wayland_SCANNER AND Wayland_PROTOCOLS_DIR)
	target_include_directories(${APP_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
	target_compile_definitions(${APP_NAME} PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
elseif(1)
	target_compile_definitions(${APP_NAME} PRIVATE VK_USE_PLATFORM_WIN32_KHR)
endif()
