cmake_minimum_required(VERSION 3.16.0)

project(VulkanTutorial)

set(PACKAGES
    01-deviceList
    02-developmentSetup
    03-funcPointers
    04-deviceInfo
    05-commandSubmission
    06-simpleImage
    07-robustImage
    08-helloTriangle
    09-helloWindow
    10-windowClass
    11-swapchain
    12-resizableWindow
    13-spinningTriangle
    14-multiplatformWindow
    15-mandelbrot
    16-input
    ff-lag
    ff-win32AndWaylandPerfection
    ff-perfInfo
   )

foreach(pkg ${PACKAGES})
	add_subdirectory(${pkg})
endforeach()
