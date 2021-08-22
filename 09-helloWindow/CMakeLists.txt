if(WIN32)

	set(APP_NAME 09-helloWindow-Win)

	project(${APP_NAME})

	set(APP_SOURCES
	    main-Win.cpp
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


		add_executable(${APP_NAME}-demo main-demo.cpp xdg-shell-client-protocol.h xdg-shell-protocol.c)
		set_property(TARGET ${APP_NAME}-demo PROPERTY CXX_STANDARD 17)

		# dependencies
		target_link_libraries(${APP_NAME}-demo Vulkan::Vulkan Wayland::client -lrt -lxkbcommon)
		target_include_directories(${APP_NAME}-demo PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

	endif()

	# 09-helloWindow-X11
	set(APP_NAME 09-helloWindow-X11)
	project(${APP_NAME})

	# dependencies
	find_package(X11)

	if(X11_FOUND)

		# target
		add_executable(${APP_NAME} main-X11.cpp)
		set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)

		# dependencies
		target_link_libraries(${APP_NAME} Vulkan::Vulkan X11)

	endif()

endif()
