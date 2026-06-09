#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "UPCXX_UTILS" for configuration "Release"
set_property(TARGET UPCXX_UTILS APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(UPCXX_UTILS PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libUPCXX_UTILS.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS UPCXX_UTILS )
list(APPEND _IMPORT_CHECK_FILES_FOR_UPCXX_UTILS "${_IMPORT_PREFIX}/lib/libUPCXX_UTILS.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
