#
# Module for finding GLFW3
#
# glfw3 target will be created and if config-based find failed,
# following variables will be created:
#    glfw3_FOUND
#    glfw3_INCLUDE_DIR
#    glfw3_LIBRARY
#    glfw3_DLL (Win32 only)
#


# try config-based find first
find_package(${CMAKE_FIND_PACKAGE_NAME} ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} CONFIG QUIET)

# find GLFW DLL
if(TARGET ${CMAKE_FIND_PACKAGE_NAME} AND WIN32)
	find_file(${CMAKE_FIND_PACKAGE_NAME}_DLL
		NAMES
			glfw3.dll
	)
endif()

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

	# find GLFW DLL
	if(WIN32)
		find_file(${CMAKE_FIND_PACKAGE_NAME}_DLL
			NAMES
				glfw3.dll
		)
	endif()

	# set *_FOUND flag
	if(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR AND ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)
		set(${CMAKE_FIND_PACKAGE_NAME}_FOUND True)
	else()
		# generate error message by find_package and by message(FATAL_ERROR)
		find_package(${CMAKE_FIND_PACKAGE_NAME} ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} CONFIG)
		message(FATAL_ERROR "Finding of package ${CMAKE_FIND_PACKAGE_NAME} failed. "
		                    "Make sure it is installed, it is of the required version and either "
		                    "(1) ${CMAKE_FIND_PACKAGE_NAME}_DIR is set to the directory of "
		                    "${CMAKE_FIND_PACKAGE_NAME}Config.cmake and ${CMAKE_FIND_PACKAGE_NAME}-config.cmake "
		                    "or (2) ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR and ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY "
		                    "are set properly.")  # FATAL_ERROR will stop CMake processing
	endif()

	# target
	if(${CMAKE_FIND_PACKAGE_NAME}_FOUND OR ${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED)
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
