#
# Module for finding GLFW3
#
# glfw target will be created
#


# try config-based find first
find_package(${CMAKE_FIND_PACKAGE_NAME} ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} CONFIG QUIET)

# use regular old-style approach
if(NOT ${CMAKE_FIND_PACKAGE_NAME}_FOUND)

	# find GLFW include path
	find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR GLFW/glfw3.h
		/usr/include
		/usr/local/include
		/opt/local/include
	)

	# find GLFW library
	find_library(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
		NAMES
			glfw3.lib glfw3_mt.lib glfw3dll.lib
		PATHS
			/usr/lib64
			/usr/local/lib64
			/usr/lib
			/usr/lib/x86_64-linux-gnu
			/usr/local/lib
	)

	# set *_FOUND flag
	if(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR AND ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)
		set(${CMAKE_FIND_PACKAGE_NAME}_FOUND True)
	endif()

	# target
	if(${CMAKE_FIND_PACKAGE_NAME}_FOUND)
		if(NOT TARGET ${CMAKE_FIND_PACKAGE_NAME})
			add_library(${CMAKE_FIND_PACKAGE_NAME} INTERFACE IMPORTED)
			set_target_properties(${CMAKE_FIND_PACKAGE_NAME} PROPERTIES
				INTERFACE_INCLUDE_DIRECTORIES "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}"
				INTERFACE_LINK_LIBRARIES "${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY}"
			)
			set(${CMAKE_FIND_PACKAGE_NAME}_DIR "${CMAKE_FIND_PACKAGE_NAME}_DIR-NOTFOUND" CACHE PATH "${CMAKE_FIND_PACKAGE_NAME} config directory." FORCE)
		endif()
	endif()

endif()
