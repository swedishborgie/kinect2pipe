cmake_minimum_required(VERSION 3.16)
project(kinect2pipe)

set(CMAKE_CXX_STANDARD 14)

find_package(PkgConfig REQUIRED)
pkg_check_modules(libswscale REQUIRED IMPORTED_TARGET libswscale)
pkg_check_modules(freenect2 REQUIRED IMPORTED_TARGET freenect2)

add_executable(kinect2pipe kinect2pipe.cpp main.cpp)
target_include_directories(kinect2pipe PRIVATE ${freenect2_INCLUDE_DIRS} ${libswscale_INCLUDE_DIRS})
target_link_libraries(kinect2pipe ${freenect2_LIBRARIES} ${libswscale_LIBRARIES} pthread)

INSTALL(TARGETS kinect2pipe DESTINATION ${CMAKE_INSTALL_BINDIR})
INSTALL(FILES "${CMAKE_SOURCE_DIR}/contrib/systemd/kinect2pipe.service" DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system)