
# find Vulkan includes
find_path(Vulkan_INCLUDE_DIR
	NAMES
		vulkan/vulkan.h
	PATHS
		"$ENV{VULKAN_SDK}/include"
		/usr/include
		/usr/local/include
)

# find Vulkan library
find_library(Vulkan_LIBRARY
	NAMES
		vulkan vulkan-1
	PATHS
		"$ENV{VULKAN_SDK}/lib"
		"$ENV{VULKAN_SDK}/lib32"
		/usr/lib64
		/usr/local/lib64
		/usr/lib
		/usr/lib/x86_64-linux-gnu
		/usr/local/lib
)

# find glslangValidator
find_program(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE
	NAMES
		glslangValidator
	PATHS
		"$ENV{VULKAN_SDK}/bin"
		"$ENV{VULKAN_SDK}/bin32"
		/usr/bin
)

# find glslc
find_program(Vulkan_GLSLC_EXECUTABLE
	NAMES
		glslc
	PATHS
		"$ENV{VULKAN_SDK}/bin"
		"$ENV{VULKAN_SDK}/bin32"
		/usr/bin
)


set(Vulkan_LIBRARIES ${Vulkan_LIBRARY})
set(Vulkan_INCLUDE_DIRS ${Vulkan_INCLUDE_DIR})

# handle required variables and set Vulkan_FOUND variable
include(FindPackageHandleStandardArgs)
string(CONCAT errorMessage
	"Finding of package ${CMAKE_FIND_PACKAGE_NAME} failed. Make sure Vulkan development files "
	"or Vulkan SDK is installed. Then, make sure Vulkan_INCLUDE_DIR and Vulkan_LIBRARY "
	"and other relevant Vulkan_* variables are set properly.")
find_package_handle_standard_args(
	${CMAKE_FIND_PACKAGE_NAME}
	REQUIRED_VARS Vulkan_LIBRARY Vulkan_INCLUDE_DIR
	REASON_FAILURE_MESSAGE ${errorMessage}
)


# Vulkan::Vulkan target
if(Vulkan_FOUND AND NOT TARGET Vulkan::Vulkan)
	add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
	set_target_properties(Vulkan::Vulkan PROPERTIES
		IMPORTED_LOCATION "${Vulkan_LIBRARIES}"
		INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}")
endif()

# Vulkan::glslangValidator target
if(Vulkan_FOUND AND Vulkan_GLSLANG_VALIDATOR_EXECUTABLE AND NOT TARGET Vulkan::glslangValidator)
	add_executable(Vulkan::glslangValidator IMPORTED)
	set_property(TARGET Vulkan::glslangValidator PROPERTY IMPORTED_LOCATION "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}")
endif()

# Vulkan::glslc target
if(Vulkan_FOUND AND Vulkan_GLSLC_EXECUTABLE AND NOT TARGET Vulkan::glslc)
  add_executable(Vulkan::glslc IMPORTED)
  set_property(TARGET Vulkan::glslc PROPERTY IMPORTED_LOCATION "${Vulkan_GLSLC_EXECUTABLE}")
endif()


# add_shaders macro to convert GLSL shaders to spir-v
# and creates depsList containing name of files that should be included among the source files
macro(add_shaders nameList depsList)
	foreach(name ${nameList})
		get_filename_component(directory ${name} DIRECTORY)
		if(directory)
			file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${directory}")
		endif()
		add_custom_command(COMMENT "Converting ${name} to spir-v..."
		                   MAIN_DEPENDENCY ${name}
		                   OUTPUT ${name}.spv
		                   COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} --target-env vulkan1.0 -x ${CMAKE_CURRENT_SOURCE_DIR}/${name} -o ${name}.spv)
		source_group("Shaders" FILES ${name} ${CMAKE_CURRENT_BINARY_DIR}/${name}.spv)
		list(APPEND ${depsList} ${name} ${CMAKE_CURRENT_BINARY_DIR}/${name}.spv)
	endforeach()
endmacro()
