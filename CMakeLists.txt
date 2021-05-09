cmake_minimum_required(VERSION 3.8.0)

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
   )

foreach(pkg ${PACKAGES})
	add_subdirectory(${pkg})
endforeach()
