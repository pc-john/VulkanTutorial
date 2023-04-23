#
# Module for finding xkbcommon
#
# If the find process is successfull, following variables will be set:
#    xkbcommon_FOUND
#    xkbcommon_INCLUDE_DIR
#    xkbcommon_LIBRARY
#
# Following targets might be created:
#    xkbcommon::xkbcommon - if xkbcommon_INCLUDE_DIR and xkbcommon_LIBRARY are set
#


# find xkbcommon include path
find_path(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR xkbcommon/xkbcommon.h
	/usr/include
	/usr/local/include
	/opt/local/include
)

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

# set *_FOUND flag and make error message
if(${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR AND ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY)
	set(${CMAKE_FIND_PACKAGE_NAME}_FOUND True)
else()
	set(${CMAKE_FIND_PACKAGE_NAME}_FOUND False)
	if(${CMAKE_FIND_PACKAGE_NAME}_FIND_REQUIRED)
		# generate error message
		message(FATAL_ERROR "Finding of package ${CMAKE_FIND_PACKAGE_NAME} failed. "
		        "Make sure it is installed and ${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR "
		        "and ${CMAKE_FIND_PACKAGE_NAME}_LIBRARY are set properly.")
		return()
	endif()
endif()

# targets
if(${CMAKE_FIND_PACKAGE_NAME}_FOUND)
	if(NOT TARGET xkbcommon::xkbcommon)
		add_library(xkbcommon::xkbcommon INTERFACE IMPORTED)
		set_target_properties(xkbcommon::xkbcommon PROPERTIES
			INTERFACE_INCLUDE_DIRECTORIES "${${CMAKE_FIND_PACKAGE_NAME}_INCLUDE_DIR}"
			INTERFACE_LINK_LIBRARIES "${${CMAKE_FIND_PACKAGE_NAME}_LIBRARY}"
		)
		set(${CMAKE_FIND_PACKAGE_NAME}_DIR "${CMAKE_FIND_PACKAGE_NAME}_DIR-NOTFOUND" CACHE PATH "${CMAKE_FIND_PACKAGE_NAME} config directory." FORCE)
	endif()
endif()
