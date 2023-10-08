#
# Module for finding GLFW3
#
# Config-based find is attempted first.
# If it fails standard detection is performed.
# If successfull, glfw3 target will be created and following variables will be set:
#    glfw3_FOUND
#    glfw3_INCLUDE_DIR
#    glfw3_LIBRARY
#    glfw3_DLL (Win32 only)
#


# try config-based find first
# but only if the user did not specified its own include dir or library
if(NOT ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR AND NOT ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)

	find_package(${CMAKE_FIND_PACKAGE_NAME} ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} CONFIG QUIET)

	# initialize cache variables
	set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR-NOTFOUND CACHE PATH "Path to ${CMAKE_FIND_PACKAGE_NAME} include directory.")
	set(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY-NOTFOUND CACHE FILEPATH "Path to ${CMAKE_FIND_PACKAGE_NAME} library.")

	# find GLFW DLL
	if(TARGET ${CMAKE_FIND_PACKAGE_NAME} AND WIN32)
		find_file(${CMAKE_FIND_PACKAGE_NAME}_DLL
			NAMES
				glfw3.dll
		)
	endif()

endif()

# use regular old-style approach
# if config-based find did not succeed
if(NOT ${CMAKE_FIND_PACKAGE_NAME}_FOUND)

	# find GLFW include path
	find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR GLFW/glfw3.h
		/usr/include
		/usr/local/include
		/opt/local/include
	)

	# message error level
	if(${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED)
		set(errorLevel FATAL_ERROR)
	else()
		set(errorLevel SEND_ERROR)
	endif()

	# check GLFW version
	if(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR AND EXISTS "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}/GLFW/glfw3.h")
		file(READ "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}/GLFW/glfw3.h" fileContent)
		string(REGEX MATCH "GLFW_VERSION_MAJOR[ ]+([0-9]*)" _ "${fileContent}")
		set(major "${CMAKE_MATCH_1}")
		string(REGEX MATCH "GLFW_VERSION_MINOR[ ]+([0-9]*)" _ "${fileContent}")
		set(minor "${CMAKE_MATCH_1}")
		if("${major}.${minor}" VERSION_LESS ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION})
			string(REGEX MATCH "GLFW_VERSION_REVISION[ ]+([0-9]*)" _ "${fileContent}")
			message(${errorLevel} "GLFW ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} or newer was not found.\n"
			        "Detected version: ${major}.${minor}.${CMAKE_MATCH_1}.\n")
			return()
		endif()
	endif()

	# find GLFW library
	if(WIN32)
		find_library(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
			NAMES
				glfw3.lib glfw3_mt.lib glfw3dll.lib
		)
	else()
		find_library(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
			NAMES
				libglfw.so libglfw.so.3
			PATHS
				/usr/lib64
				/usr/local/lib64
				/usr/lib
				/usr/lib/x86_64-linux-gnu
				/usr/local/lib
		)
	endif()

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
		# generate error message
		if(${CMAKE_FIND_PACKAGE_NAME}_CONSIDERED_VERSIONS)
			message(${errorLevel} "GLFW ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} or newer was not found.\n"
			        "Considered versions: ${${CMAKE_FIND_PACKAGE_NAME}_CONSIDERED_VERSIONS}\n"
			        "Considered configs: ${${CMAKE_FIND_PACKAGE_NAME}_CONSIDERED_CONFIGS}\n")
		else()
			message(${errorLevel} "Finding of package ${CMAKE_FIND_PACKAGE_NAME} failed. "
			        "Make sure it is installed, it is of the version "
			        "${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} or newer, and either "
			        "(1) ${CMAKE_FIND_PACKAGE_NAME}_DIR is set to the directory of "
			        "${CMAKE_FIND_PACKAGE_NAME}Config.cmake and ${CMAKE_FIND_PACKAGE_NAME}-config.cmake "
			        "or (2) ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR and "
			        "${CMAKE_FIND_PACKAGE_NAME}_LIBRARY are set properly.")
		endif()
		return()
	endif()

	# target
	if(${CMAKE_FIND_PACKAGE_NAME}_FOUND OR ${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED)
		if(NOT TARGET glfw)
			add_library(glfw INTERFACE IMPORTED)
			set_target_properties(glfw PROPERTIES
				INTERFACE_INCLUDE_DIRECTORIES "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}"
				INTERFACE_LINK_LIBRARIES "${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY}"
			)
			set(${CMAKE_FIND_PACKAGE_NAME}_DIR "${CMAKE_FIND_PACKAGE_NAME}_DIR-NOTFOUND" CACHE PATH "${CMAKE_FIND_PACKAGE_NAME} config directory." FORCE)
		endif()
	endif()

endif()
