cmake_minimum_required(VERSION 3.0)
project( wsServer )
find_package( OpenCV REQUIRED )
include_directories( ${OpenCV_INCLUDE_DIRS} )
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
find_package( websocketpp REQUIRED)
include_directories( ${websocketpp_INCLUDE_DIRS} )

find_package(libjpeg-turbo REQUIRED PATHS /home/randymoore/vcpkg/installed/x64-linux/share/libjpeg-turbo)
include_directories(libjpeg-turbo PATHS /home/randymoore/vcpkg/installed/x64-linux/include)

add_executable( wsServer main.cpp )
target_link_libraries( wsServer ${OpenCV_LIBS} websocketpp::websocketpp /home/randymoore/vcpkg/installed/x64-linux/lib/libjpeg.a /home/randymoore/vcpkg/installed/x64-linux/lib/libturbojpeg.a)
