# use upcxx as the compiler

FILE(GLOB UPCXX_UTILS_SOURCE_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

INCLUDE_DIRECTORIES(${PROJECT_BINARY_DIR}) # to include the autogen version file

if (${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.13 AND DEFINED UPCXX_LIBRARIES)
  message(STATUS "UPCXX_UTILS is using the UPCXX::upcxx library interface: ${UPCXX_LIBRARIES}")
else()
  find_program(UPCXX_EXEC upcxx)
  set(CMAKE_CXX_COMPILER ${UPCXX_EXEC})
  message(STATUS "UPCXX_UTILS is using upcxx directly as the UPCXX::upcxx library interface is not available in this low version of cmake: ${CMAKE_VERSION}")
endif()

set(upcxx_utils_libs log progress_bar timers two_tier_aggr_store split_rank mem_profile)
set(AND_THREADS)
if (THREADS_FOUND AND NOT UPCXX_UTILS_NO_THREADS)
  set(AND_THREADS Threads::Threads)
endif()

list(APPEND upcxx_utils_libs_targets $<TARGET_OBJECTS:UPCXX_UTILS_VERSION>)

foreach(_lib ${upcxx_utils_libs})

  set(_libsname upcxx_utils_${_lib})
  add_library(${_libsname} OBJECT ${_lib}.cpp)
  if (${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.13 AND DEFINED UPCXX_LIBRARIES)
    target_link_libraries(${_libsname} PUBLIC ${UPCXX_LIBRARIES})
  endif()
  target_include_directories(${_libsname} INTERFACE
                       $<BUILD_INTERFACE:${UPCXX_UTILS_SOURCE_DIR}/include> # for headers when building
                       $<INSTALL_INTERFACE:include> # for client in install mode
  )

  # append to the list for aggreated library
  list(APPEND upcxx_utils_libs_targets $<TARGET_OBJECTS:${_libsname}>)

endforeach()

# the aggregate library
add_library(UPCXX_UTILS ${upcxx_utils_libs_targets})
if (${CMAKE_VERSION} VERSION_GREATER_EQUAL 3.13 AND DEFINED UPCXX_LIBRARIES)
  target_link_libraries(UPCXX_UTILS PUBLIC ${UPCXX_LIBRARIES} ${AND_THREADS})
endif()
target_include_directories(UPCXX_UTILS INTERFACE
                       $<BUILD_INTERFACE:${UPCXX_UTILS_SOURCE_DIR}/include> # for headers when building
                       $<INSTALL_INTERFACE:include> # for client in install mode
)
install(TARGETS UPCXX_UTILS EXPORT UPCXX_UTILS DESTINATION "lib")

