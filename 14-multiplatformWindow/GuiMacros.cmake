macro(GuiConfigure APP_SOURCES APP_INCLUDES libs defines vulkanWindowDefines includes)

	# set GUI_TYPE if not already set or if set to "default" string
	string(TOLOWER "${GUI_TYPE}" guiTypeLowerCased)
	if(NOT GUI_TYPE OR "${guiTypeLowerCased}" STREQUAL "default")

		# detect recommended GUI type
		if(WIN32)
			set(guiTypeDetected "Win32")
		elseif(UNIX)
			find_package(Wayland)
			if(Wayland_client_FOUND AND Wayland_SCANNER AND Wayland_PROTOCOLS_DIR)
				set(guiTypeDetected "Wayland")
			else()
				find_package(X11)
				if(X11_FOUND)
					set(guiTypeDetected "Xlib")
				else()
					# default to Wayland on Linux
					set(guiTypeDetected "Wayland")
				endif()
			endif()
		endif()
		set(GUI_TYPE ${guiTypeDetected} CACHE STRING "Gui type. Accepted values: default, Win32, Xlib, Wayland, SDL, GLFW, Qt6 and Qt5." FORCE)

	endif()

	# give error on invalid GUI_TYPE
	set(guiList "Win32" "Xlib" "Wayland" "SDL" "GLFW" "Qt6" "Qt5")
	if(NOT "${GUI_TYPE}" IN_LIST guiList)
		message(FATAL_ERROR "GUI_TYPE value is invalid. It must be set to default, Win32, Xlib, Wayland, SDL, GLFW, Qt6 or Qt5.")
	endif()


	if("${GUI_TYPE}" STREQUAL "Win32")

		# configure for Win32
		set(${defines} ${${defines}} USE_PLATFORM_WIN32)
		set(${vulkanWindowDefines} ${${vulkanWindowDefines}} VK_USE_PLATFORM_WIN32_KHR)

	elseif("${GUI_TYPE}" STREQUAL "Xlib")

		# configure for Xlib
		find_package(X11 REQUIRED)
		set(${libs} ${${libs}} X11)
		set(${defines} ${${defines}} USE_PLATFORM_XLIB)
		set(${vulkanWindowDefines} ${${vulkanWindowDefines}} VK_USE_PLATFORM_XLIB_KHR)

	elseif("${GUI_TYPE}" STREQUAL "Wayland")

		# configure for Wayland
		find_package(Wayland REQUIRED)
		if(Wayland_client_FOUND AND Wayland_SCANNER AND Wayland_PROTOCOLS_DIR)

			add_custom_command(OUTPUT xdg-shell-client-protocol.h
			                   COMMAND ${Wayland_SCANNER} client-header ${Wayland_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h)
			add_custom_command(OUTPUT xdg-shell-protocol.c
			                   COMMAND ${Wayland_SCANNER} private-code  ${Wayland_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c)
			add_custom_command(OUTPUT xdg-decoration-client-protocol.h
			                   COMMAND ${Wayland_SCANNER} client-header ${Wayland_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-client-protocol.h)
			add_custom_command(OUTPUT xdg-decoration-protocol.c
			                   COMMAND ${Wayland_SCANNER} private-code  ${Wayland_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-protocol.c)

			list(APPEND ${APP_SOURCES}  xdg-shell-protocol.c        xdg-decoration-protocol.c)
			list(APPEND ${APP_INCLUDES} xdg-shell-client-protocol.h xdg-decoration-client-protocol.h)
			set(${libs} ${${libs}} Wayland::client -lrt)
			set(${defines} ${${defines}} USE_PLATFORM_WAYLAND)
			set(${vulkanWindowDefines} ${${vulkanWindowDefines}} VK_USE_PLATFORM_WAYLAND_KHR)

		else()
			message(FATAL_ERROR "Not all Wayland variables were detected properly.")
		endif()

	elseif("${GUI_TYPE}" STREQUAL "SDL")

		# configure for SDL
		find_package(SDL REQUIRED)
		set(${libs} ${${libs}} ${SDL_LIBRARY} ${SDLMAIN_LIBRARY})
		set(${defines} ${${defines}} USE_PLATFORM_SDL)
		set(${includes} ${${includes}} ${SDL_INCLUDE_DIR})

	elseif("${GUI_TYPE}" STREQUAL "Qt6")

		# configure for Qt6
		find_package(Qt6Gui REQUIRED)
		set(${libs} ${${libs}} Qt6::Gui)
		set(${defines} ${${defines}} USE_PLATFORM_QT)

	elseif("${GUI_TYPE}" STREQUAL "Qt5")

		# configure for Qt5
		# (we need at least version 5.10 because of Vulkan support)
		find_package(Qt5Gui 5.10 REQUIRED)
		set(${libs} ${${libs}} Qt5::Gui)
		set(${defines} ${${defines}} USE_PLATFORM_QT)

	else()
		message(FATAL_ERROR "Invalid GUI_TYPE value: ${GUI_TYPE}")
	endif()

endmacro()
