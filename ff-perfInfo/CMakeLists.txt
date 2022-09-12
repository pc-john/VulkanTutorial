set(APP_NAME ff-perfInfo)

project(${APP_NAME})

set(APP_SOURCES
    main.cpp
   )

set(APP_INCLUDES
   )

set(APP_SHADERS
    shader.comp
   )

# target
add_shaders("${APP_SHADERS}" APP_SHADER_DEPS)
add_executable(${APP_NAME} ${APP_SOURCES} ${APP_INCLUDES} ${APP_SHADER_DEPS})
set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)

# dependencies
find_package(Vulkan REQUIRED)
target_include_directories(${APP_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${APP_NAME} Vulkan::Vulkan)
