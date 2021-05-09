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

	# dependencies
	set(CMAKE_MODULE_PATH "${${APP_NAME}_SOURCE_DIR}/;${CMAKE_MODULE_PATH}")
	find_package(Vulkan REQUIRED)
	find_package(Wayland)
	find_package(X11)

	if(WAYLAND_FOUND)

		set(APP_NAME tut09-helloWindow-Wayland)

		project(${APP_NAME})

		set(APP_SOURCES
		    main-Wayland.cpp
		   )

		set(APP_INCLUDES
		   )

		# target
		add_executable(${APP_NAME} ${APP_SOURCES} ${APP_INCLUDES})
		set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)

		# dependencies
		target_link_libraries(${APP_NAME} Vulkan::Vulkan ${WAYLAND_CLIENT_LIBRARIES})

	endif()

	if(X11_FOUND)

		set(APP_NAME 09-helloWindow-X11)

		project(${APP_NAME})

		set(APP_SOURCES
		    main-X11.cpp
		   )

		set(APP_INCLUDES
		   )

		# target
		add_executable(${APP_NAME} ${APP_SOURCES} ${APP_INCLUDES})
		set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)

		# dependencies
		target_link_libraries(${APP_NAME} Vulkan::Vulkan X11)

	endif()

endif()
