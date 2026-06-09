# The name of our project is "UPCXX_UTILS". CMakeLists files in this project can
# refer to the root source directory of the project as ${UPCXX_UTILS_SOURCE_DIR} and
# to the root binary directory of the project as ${UPCXX_UTILS_BINARY_DIR}.
cmake_minimum_required (VERSION 3.8 FATAL_ERROR)

#----------------------------------------------------------------------------------------#
#   project specification
#----------------------------------------------------------------------------------------#
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION UPCXX_UTILS_BASE_VERSION)
project(UPCXX_UTILS VERSION ${UPCXX_UTILS_BASE_VERSION} DESCRIPTION "Utilties that can be used within the framework of UPC++" LANGUAGES CXX)

# install exported targets
export(PACKAGE UPCXX_UTILS)
install(EXPORT UPCXX_UTILS DESTINATION "cmake")
#install(FILES cmake/FindUPCXX_UTILS.cmake cmake/UPCXX_UTILSConfig.cmake DESTINATION "cmake")
install(FILES cmake/UPCXX_UTILSConfig.cmake DESTINATION "cmake")

# use PROJECT_SOURCE_DIR, not CMAKE_SOURCE_DIR
set(UPCXX_UTILS_SOURCE_DIR ${PROJECT_SOURCE_DIR})
set(UPCXX_UTILS_BINARY_DIR ${PROJECT_BINARY_DIR})

# include modules for this package
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake/Modules)

# check compiler and set appropriate linker flags
include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(CheckCXXSourceRuns)
include(GetGitVersion)

# enable testing
option(UPCXX_ENABLE_TESTING "Enable testing of upcxx-utils" ON)
if(UPCXX_ENABLE_TESTING)
  enable_testing()
endif()

# all c++ must be at least c++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT APPLE)
    add_definitions(-D_XOPEN_SOURCE=700)
endif()

# prefer static to dynamic libraries when including third party libs (like ZLIB)
if (APPLE)
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".dylib" ".so")
else()
  set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ".so")
endif()

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
	    FORCE)
endif()
message(STATUS "Building for ${CMAKE_BUILD_TYPE}")
if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    add_definitions(-DDEBUG)
endif()

# set version
include_directories(include)
GET_GIT_VERSION(UPCXX_UTILS)

option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_STATIC_LIBS "Build static libraries" ON)
option(CMAKE_VERBOSE_MAKEFILE "Verbose makefile" OFF)
option(UPCXX_UTILS_NO_THREADS "Enable thread libs" ${UPCXX_UTILS_NO_THREADS})

message(STATUS "Testing the environment..")
get_directory_property(hasParent PARENT_DIRECTORY)

# find upcxx, and bootstrap a recent copy of FindUPCXX.cmake if necessary
find_package(UPCXX)
message(STATUS "Using UPCXX version - ${UPCXX_VERSION} ${UPCXX_VERSION_STRING}")

if (UPCXX_UTILS_NO_THREADS)
  add_definitions(-DUPCXX_UTILS_NO_THREADS)
  message(STATUS "UPCXX_UTILS *disabling* thread libraries")
else()
  find_package(Threads REQUIRED)
endif()

include_directories("${PROJECT_SOURCE_DIR}/include")
add_subdirectory("src")
if (UPCXX_ENABLE_TESTING)
  add_subdirectory("test")
endif()

# The interface library
add_library(UPCXX_UTILS_INTERFACE_LIB INTERFACE)
set_target_properties( UPCXX_UTILS_INTERFACE_LIB PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "$<BUILD_INTERFACE:${UPCXX_UTILS_SOURCE_DIR}/include>;$<INSTALL_INTERFACE:include>"
    INTERFACE_LINK_LIBRARIES      UPCXX_UTILS
    )

# include a version file
include(CMakePackageConfigHelpers)
message(STATUS "Installing package version ${UPCXX_UTILS_VERSION}")
#set(export_version_file "${CMAKE_CURRENT_BINARY_DIR}/makeVersionFile/UPCXX_UTILSVersion.cmake")
set(export_version_file "${CMAKE_CURRENT_BINARY_DIR}/makeVersionFile/UPCXX_UTILSConfigVersion.cmake")
write_basic_package_version_file(
  ${export_version_file}
  VERSION ${UPCXX_UTILS_VERSION}
  COMPATIBILITY AnyNewerVersion
)
install(FILES ${export_version_file} ${UPCXX_UTILS_VERSION_FILE} DESTINATION "cmake")

install(TARGETS UPCXX_UTILS_INTERFACE_LIB EXPORT UPCXX_UTILS)
if (hasParent)
  set(UPCXX_UTILS_LIBRARIES UPCXX_UTILS_INTERFACE_LIB PARENT_SCOPE)
endif()

# install the header files
FILE(GLOB UPCXX_UTILS_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/upcxx_utils/*.h*)
install(FILES include/upcxx_utils.hpp DESTINATION "include")
install(FILES ${UPCXX_UTILS_HEADERS} DESTINATION "include/upcxx_utils")
install(FILES LICENSE.txt LEGAL.txt README.md DESTINATION ".") 


message(STATUS "UPCXX_UTILS-utils build type: ${CMAKE_BUILD_TYPE}")
