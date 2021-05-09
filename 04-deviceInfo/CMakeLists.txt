set(APP_NAME 04-deviceInfo)

project(${APP_NAME})

set(APP_SOURCES
    main.cpp
   )

set(APP_INCLUDES
   )

# target
add_executable(${APP_NAME} ${APP_SOURCES} ${APP_INCLUDES})
set_property(TARGET ${APP_NAME} PROPERTY CXX_STANDARD 17)

# dependencies
find_package(Vulkan REQUIRED)
target_link_libraries(${APP_NAME} Vulkan::Vulkan)
