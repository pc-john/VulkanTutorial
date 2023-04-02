#
# Module for finding xkbcommon
#
# Config-based find is attempted first.
# If it fails standard detection is performed.
# If successfull, xkbcommon::xkbcommon target will be created and following variables will be set:
#    xkbcommon_FOUND
#    xkbcommon_INCLUDE_DIR
#    xkbcommon_LIBRARY
#


# find xkbcommon include path
find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR xkbcommon/xkbcommon.h
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

# find xkbcommon library
find_library(${CMAKE_FIND_PACKAGE_NAME}_LIBRARY
	NAMES
		libxkbcommon.so
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
else()
	# generate error message
	message(${errorLevel} "Finding of package ${CMAKE_FIND_PACKAGE_NAME} failed. "
				"Make sure it is installed and either "
				"(1) ${CMAKE_FIND_PACKAGE_NAME}_DIR is set to the directory of "
				"${CMAKE_FIND_PACKAGE_NAME}Config.cmake and ${CMAKE_FIND_PACKAGE_NAME}-config.cmake "
				"or (2) ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR and "
				"${CMAKE_FIND_PACKAGE_NAME}_LIBRARY are set properly.")
	return()
endif()

# target
if(${CMAKE_FIND_PACKAGE_NAME}_FOUND OR ${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED)
	if(NOT TARGET xkbcommon::xkbcommon)
		add_library(xkbcommon::xkbcommon INTERFACE IMPORTED)
		set_target_properties(xkbcommon::xkbcommon PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}"
			INTERFACE_LINK_LIBRARIES "${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY}"
		)
		set(${CMAKE_FIND_PACKAGE_NAME}_DIR "${CMAKE_FIND_PACKAGE_NAME}_DIR-NOTFOUND" CACHE PATH "${CMAKE_FIND_PACKAGE_NAME} config directory." FORCE)
	endif()
endif()
