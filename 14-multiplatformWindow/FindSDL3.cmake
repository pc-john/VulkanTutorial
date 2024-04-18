#
# Module for finding SDL3
#
# It attempts to perform config-based find first. If it fails, it performs standard module-based find.
# In config-based find succeeds, SDL3 targets are created and SDL3_DLL cache variable is set (if it was undefined).
# If config-based find fails, no targets are created. Instead, the following variables are defined:
#    SDL3_FOUND
#    SDL3_INCLUDE_DIRS
#    SDL3_LIBRARIES
#    SDL3_DLL (on Win32 only)
#


# try config-based find first
# but only if the user did not specified its own include dir or library
if(NOT ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR AND NOT ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)

	find_package(${CMAKE_FIND_PACKAGE_NAME} ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} CONFIG QUIET)

	# initialize cache variables
	set(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR-NOTFOUND CACHE PATH "Path to ${CMAKE_FIND_PACKAGE_NAME} include directory.")
	set(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY-NOTFOUND CACHE FILEPATH "Path to ${CMAKE_FIND_PACKAGE_NAME} library.")

	# get SDL3_DLL
	if(TARGET SDL3::SDL3 AND NOT SDL3_DLL AND WIN32)
		get_target_property(SDL3_DLL SDL3::SDL3 IMPORTED_LOCATION)
		cmake_path(NORMAL_PATH SDL3_DLL)
		set(SDL3_DLL "${SDL3_DLL}" CACHE FILEPATH "Path to SDL3.dll that will be copied into the directory of built executable." FORCE)
	endif()

endif()

# use regular old-style approach
# if config-based find did not succeed
if(NOT ${CMAKE_FIND_PACKAGE_NAME}_FOUND)

	# find SDL3 include path
	find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR SDL3/SDL.h
		PATHS
			/usr/include
			/usr/local/include
			/opt/local/include
	)

	# find SDL3 library
	find_library(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
		NAMES
			SDL3
		PATHS
			/usr/lib64
			/usr/local/lib64
			/usr/lib
			/usr/lib/x86_64-linux-gnu
			/usr/local/lib
	)

	# find SDL3_DLL library
	if(WIN32)
		find_file(${CMAKE_FIND_PACKAGE_NAME}_DLL
			NAMES
				SDL3.dll
		)
	endif()

	# set output variables
	if(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR AND ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)
		set(${CMAKE_FIND_PACKAGE_NAME}_FOUND True)
		set(SDL3_INCLUDE_DIRS "${SDL3_INCLUDE_DIR}")
		set(SDL3_LIBRARIES    "${SDL3_LIBRARY}")
	else()
		# error message
		message(FATAL_ERROR "Finding of package ${CMAKE_FIND_PACKAGE_NAME} failed. Make sure it is installed "
		                    "and either (1) ${CMAKE_FIND_PACKAGE_NAME}_DIR is set to the directory of "
		                    "${CMAKE_FIND_PACKAGE_NAME}Config.cmake and ${CMAKE_FIND_PACKAGE_NAME}-config.cmake "
		                    "or (2) ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR and ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY "
		                    "and other relevant ${CMAKE_FIND_PACKAGE_NAME}_* variables are set properly.")  # FATAL_ERROR will stop CMake processing
	endif()

endif()
