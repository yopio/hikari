cmake_minimum_required(VERSION 3.7 FATAL_ERROR)
project(hikari)

enable_language(CXX)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

find_package(Vulkan)
if(NOT Vulkan_FOUND)
  message(FATAL_ERROR "Could not find Vulkan library.")
else()
  message(STATUS ${Vulkan_LIBRARY})
endif()

find_package(PkgConfig REQUIRED)
pkg_search_module(GLFW REQUIRED glfw3)
if(NOT ${GLFW_FOUND})
  message(FATAL_ERROR "Could not find GLFW library.")
else()
  message(STATUS ${GLFW_LIBRARIES})
endif()

file(GLOB SOURCE ./src/main.cc)
add_executable(${PROJECT_NAME} ${SOURCE})
target_link_libraries(${PROJECT_NAME} ${GLFW_LIBRARIES} ${Vulkan_LIBRARY} ${CMAKE_DL_LIBS})

# CMAKE_DL_LIBS is required to link dlopen, dlclose etc...
