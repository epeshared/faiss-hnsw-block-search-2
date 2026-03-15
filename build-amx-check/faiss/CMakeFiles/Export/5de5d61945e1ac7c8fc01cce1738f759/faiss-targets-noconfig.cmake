#----------------------------------------------------------------
# Generated CMake target import file.
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "faiss" for configuration ""
set_property(TARGET faiss APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(faiss PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libfaiss.a"
  )

list(APPEND _cmake_import_check_targets faiss )
list(APPEND _cmake_import_check_files_for_faiss "${_IMPORT_PREFIX}/lib/libfaiss.a" )

# Import target "faiss_avx2" for configuration ""
set_property(TARGET faiss_avx2 APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(faiss_avx2 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libfaiss_avx2.a"
  )

list(APPEND _cmake_import_check_targets faiss_avx2 )
list(APPEND _cmake_import_check_files_for_faiss_avx2 "${_IMPORT_PREFIX}/lib/libfaiss_avx2.a" )

# Import target "faiss_avx512_spr" for configuration ""
set_property(TARGET faiss_avx512_spr APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(faiss_avx512_spr PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG "CXX"
  IMPORTED_LOCATION_NOCONFIG "${_IMPORT_PREFIX}/lib/libfaiss_avx512_spr.a"
  )

list(APPEND _cmake_import_check_targets faiss_avx512_spr )
list(APPEND _cmake_import_check_files_for_faiss_avx512_spr "${_IMPORT_PREFIX}/lib/libfaiss_avx512_spr.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
