cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

#
#
# @TODO: Change this project to the name of your project
#
#
project("sgct-networked-example")

#
# Add the libraries that this application depends on,
#  1. SGCT (https://github.com/sgct/sgct) for window handling and cluster support
#  2. libwebsockets (https://libwebsockets.org) for support for WebSockets

# Disable SSL requirement as it requires another library
set(LWS_WITH_SSL OFF CACHE BOOL "" FORCE)
set(LWS_WITHOUT_TESTAPPS ON CACHE BOOL "" FORCE)
add_subdirectory(ext/libwebsockets)
set_property(TARGET dist PROPERTY FOLDER "Dependencies")
set_property(TARGET websockets PROPERTY FOLDER "Dependencies")
set_property(TARGET websockets_shared PROPERTY FOLDER "Dependencies")

add_subdirectory(ext/sgct)
set_property(TARGET sgct PROPERTY FOLDER "Dependencies")

find_package(glm REQUIRED)


#
# Adding the source files here that are compiled for this project
#
add_executable(${PROJECT_NAME}
  src/main.cpp
  src/websockethandler.h
  src/websockethandler.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE
  src
  ext/sgct/include
  ext/libwebsockets/include
  ${LIBWEBSOCKETS_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME} PRIVATE sgct websockets glm::glm)


#
# Setting some compile settings for the project
#
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

if (MSVC)
  # Microsoft Visual Studio related compile options
  target_compile_options(${PROJECT_NAME} PRIVATE
    "/MP"       # Multi-threading support
    "/W4"       # Highest warning level
    "/wd4201"   # nonstandard extension used : nameless struct/union
    "/permissive-"
    "/Zc:__cplusplus" # Correctly set the __cplusplus macro
  )
elseif (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # When compiling on Clang.  This most likely means compiling on MacOS
  target_compile_options(${PROJECT_NAME} PRIVATE
    "-stdlib=libc++"
    "-Wall"
    "-Wextra"
  )
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  # Probably compiling on Linux
  target_compile_options(${PROJECT_NAME} PRIVATE
    "-ggdb"
    "-Wall"
    "-Wextra"
    "-Wpedantic"
  )
endif ()
