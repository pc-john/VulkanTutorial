#
# Module for finding SDL2
#
# It attempts to perform config-based find first. If it fails, it performs standard module-based find.
# In config-based find succeeds, SDL2 targets are created and SDL2_DLL cache variable is set (if it was undefined).
# If config-based find fails, no targets are created. Instead, the following variables are defined:
#    SDL2_FOUND
#    SDL2_INCLUDE_DIRS
#    SDL2_LIBRARIES
#    SDL2_DLL (on Win32 only)
#


# try config-based find first
find_package(${CMAKE_FIND_PACKAGE_NAME} ${${CMAKE_FIND_PACKAGE_NAME}_FIND_VERSION} CONFIG QUIET)

# get SDL2_DLL
if(TARGET SDL2::SDL2 AND NOT DEFINED SDL2_DLL)
	get_target_property(SDL2_DLL SDL2::SDL2 IMPORTED_LOCATION)
	cmake_path(NORMAL_PATH SDL2_DLL)
	set(SDL2_DLL "${SDL2_DLL}" CACHE FILEPATH "Path to SDL2.dll that will be copied into the directory of built executable.")
endif()

# use regular old-style approach
if(NOT ${CMAKE_FIND_PACKAGE_NAME}_FOUND)

	# find SDL2 include path
	find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR SDL.h
		/usr/include
		/usr/local/include
		/opt/local/include
	)

	# find SDL2 library
	find_library(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
		NAMES
			SDL2
		PATHS
			/usr/lib64
			/usr/local/lib64
			/usr/lib
			/usr/lib/x86_64-linux-gnu
			/usr/local/lib
	)
	find_library(${CMAKE_FIND_PACKAGE_NAME}_MAIN_LIBRARY
		NAMES
			SDL2main
		PATHS
			/usr/lib64
			/usr/local/lib64
			/usr/lib
			/usr/lib/x86_64-linux-gnu
			/usr/local/lib
	)

	# find SDL2_DLL library
	if(WIN32)
		find_file(${CMAKE_FIND_PACKAGE_NAME}_DLL
			NAMES
				SDL2.dll
		)
	endif()

	# set *_FOUND flag
	if(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR AND ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)
		set(${CMAKE_FIND_PACKAGE_NAME}_FOUND True)
		set(SDL2_INCLUDE_DIRS "${SDL2_INCLUDE_DIR}")
		if(SDL2_MAIN_LIBRARY)
			set(SDL2_LIBRARIES    "${SDL2_LIBRARY}" "${SDL2_MAIN_LIBRARY}")
		else()
			set(SDL2_LIBRARIES    "${SDL2_LIBRARY}")
		endif()
	else()
		message(FATAL_ERROR "Finding of package ${CMAKE_FIND_PACKAGE_NAME} failed. Make sure it is installed "
		                    "and either (1) ${CMAKE_FIND_PACKAGE_NAME}_DIR is set to the directory of "
		                    "${CMAKE_FIND_PACKAGE_NAME}Config.cmake and ${CMAKE_FIND_PACKAGE_NAME}-config.cmake "
		                    "or (2) ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR and ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY "
		                    "and other relevant ${CMAKE_FIND_PACKAGE_NAME}_* variables are set properly.")  # FATAL_ERROR will stop CMake processing
	endif()

endif()
