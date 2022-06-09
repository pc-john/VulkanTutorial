set(APP_NAME 11-swapchain)

project(${APP_NAME})

set(APP_SOURCES
    main.cpp
    VulkanWindow.cpp
   )

set(APP_INCLUDES
    VulkanWindow.h
   )

# dependencies
find_package(Vulkan REQUIRED)
set(libs Vulkan::Vulkan)

# GUI dependencies
set(CMAKE_MODULE_PATH "${${APP_NAME}_SOURCE_DIR}/;${CMAKE_MODULE_PATH}")
include(GuiMacros.cmake)
GuiConfigure(APP_SOURCES APP_INCLUDES libs defines vulkanWindowDefines includes)

# executable
add_executable(${APP_NAME} ${APP_SOURCES} ${APP_INCLUDES})

# target
target_include_directories(${APP_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${includes})
target_compile_definitions(${APP_NAME} PRIVATE ${defines})
target_link_libraries(${APP_NAME} ${libs})
set_property(SOURCE VulkanWindow.cpp APPEND PROPERTY COMPILE_DEFINITIONS ${vulkanWindowDefines})
set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)
