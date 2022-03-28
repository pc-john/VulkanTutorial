if(WIN32)

	set(APP_NAME 09-helloWindow-Win32)

	project(${APP_NAME})

	set(APP_SOURCES
	    main-Win32.cpp
	   )

	set(APP_INCLUDES
	   )

	# target
	add_executable(${APP_NAME} ${APP_SOURCES} ${APP_INCLUDES})
	set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)

	# dependencies
	find_package(Vulkan REQUIRED)
	target_link_libraries(${APP_NAME} Vulkan::Vulkan)

endif()

if(UNIX)

	# 09-helloWindow-Wayland
	set(APP_NAME 09-helloWindow-Wayland)
	project(${APP_NAME})

	# dependencies
	set(CMAKE_MODULE_PATH "${${APP_NAME}_SOURCE_DIR}/;${CMAKE_MODULE_PATH}")
	find_package(Vulkan REQUIRED)
	find_package(Wayland)

	# Wayland requires following variables to be set:
	# Wayland_client_INCLUDE_DIR and Wayland_client_LIBRARY - on ubuntu 21.10, it is provided by libwayland-dev package
	# Wayland_SCANNER - on ubuntu 21.10, it is provided by libwayland-bin package
	# Wayland_PROTOCOLS_DIR - on ubuntu 21.10, it is provided by wayland-protocols package,
	#                         to have the path detected automatically, pkg-config package is needed
	if(Wayland_client_FOUND AND Wayland_SCANNER AND Wayland_PROTOCOLS_DIR)

		# target
		add_custom_command(OUTPUT xdg-shell-client-protocol.h
		                   COMMAND ${Wayland_SCANNER} client-header ${Wayland_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h)
		add_custom_command(OUTPUT xdg-shell-protocol.c
		                   COMMAND ${Wayland_SCANNER} private-code ${Wayland_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c)
		add_executable(${APP_NAME} main-Wayland.cpp xdg-shell-client-protocol.h xdg-shell-protocol.c)
		set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)

		# dependencies
		target_link_libraries(${APP_NAME} Vulkan::Vulkan Wayland::client -lrt)
		target_include_directories(${APP_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

	endif()

	# 09-helloWindow-X11
	set(APP_NAME 09-helloWindow-Xlib)
	project(${APP_NAME})

	# dependencies
	find_package(X11)

	# X11 development files are provided by libx11-dev package on ubuntu 21.10
	if(X11_FOUND)

		# target
		add_executable(${APP_NAME} main-Xlib.cpp)
		set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)

		# dependencies
		target_link_libraries(${APP_NAME} Vulkan::Vulkan X11)

	endif()

endif()
