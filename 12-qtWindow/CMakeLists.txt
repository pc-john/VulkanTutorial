set(APP_NAME 12-qtWindow)

project(${APP_NAME})

set(APP_SOURCES
    main.cpp
    VulkanWindow.cpp
   )

set(APP_INCLUDES
    VulkanWindow.h
   )

set(APP_SHADERS
    shader.vert
    shader.frag
   )

# dependencies
find_package(Vulkan REQUIRED)
set(libs Vulkan::Vulkan)

# GUI dependencies
set(CMAKE_MODULE_PATH "${${APP_NAME}_SOURCE_DIR}/;${CMAKE_MODULE_PATH}")
include(GuiMacros.cmake)
GuiConfigure(APP_SOURCES APP_INCLUDES libs defines vulkanWindowDefines includes)

# executable
add_shaders("${APP_SHADERS}" APP_SHADER_DEPS)
add_executable(${APP_NAME} ${APP_SOURCES} ${APP_INCLUDES} ${APP_SHADER_DEPS})

# target
target_include_directories(${APP_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${includes})
target_compile_definitions(${APP_NAME} PRIVATE ${defines})
set_property(SOURCE VulkanWindow.cpp APPEND PROPERTY COMPILE_DEFINITIONS ${vulkanWindowDefines})
target_link_libraries(${APP_NAME} ${libs})
set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)
