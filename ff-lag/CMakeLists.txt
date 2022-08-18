set(APP_NAME ff-lag)

project(${APP_NAME})

set(APP_SOURCES
    main.cpp
    VulkanWindow.cpp
    Timestamps.cpp
   )

set(APP_INCLUDES
    VulkanWindow.h
    Timestamps.h
   )

set(APP_SHADERS
    shader.vert
    shader.frag
   )

# dependencies
set(CMAKE_MODULE_PATH "${${APP_NAME}_SOURCE_DIR}/;${CMAKE_MODULE_PATH}")
find_package(Vulkan REQUIRED)
set(libs Vulkan::Vulkan)

# GUI dependencies
include(GuiMacros.cmake)
GuiConfigure(APP_SOURCES APP_INCLUDES libs defines vulkanWindowDefines includes)

# executable
add_shaders("${APP_SHADERS}" APP_SHADER_DEPS)
add_executable(${APP_NAME} ${APP_SOURCES} ${APP_INCLUDES} ${APP_SHADER_DEPS})

# target
target_include_directories(${APP_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${includes})
target_compile_definitions(${APP_NAME} PRIVATE ${defines})
target_link_libraries(${APP_NAME} ${libs})
set_property(SOURCE VulkanWindow.cpp APPEND PROPERTY COMPILE_DEFINITIONS ${vulkanWindowDefines})
set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)
