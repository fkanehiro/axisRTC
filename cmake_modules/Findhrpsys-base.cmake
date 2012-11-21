set(HRPSYS_BASE_FOUND TRUE)

execute_process(
  COMMAND pkg-config --variable=prefix hrpsys-base
  OUTPUT_VARIABLE HRPSYS_BASE_DIR
  RESULT_VARIABLE RESULT
  OUTPUT_STRIP_TRAILING_WHITESPACE)

if(RESULT EQUAL 0)
  if(HRPSYS_BASE_DIR)
	list(APPEND HRPSYS_BASE_INCLUDE_DIRS "${HRPSYS_BASE_DIR}/include/hrpsys/idl")
        list(APPEND HRPSYS_BASE_LIBRARY_DIRS "${HRPSYS_BASE_DIR}/lib")
  endif()
else()
  set(HRPSYS_BASE_FOUND FALSE)
endif()

if(NOT HRPSYS_BASE_FOUND)
  set(HRPSYS_BASE_DIR NOT_FOUND)
endif()

set(HRPSYS_BASE_DIR ${HRPSYS_BASE_DIR} CACHE PATH "The top directory of HRPSYS-BASE")

if(HRPSYS_BASE_FOUND)
  if(NOT HRPSYS_BASE_FIND_QUIETLY)
    message(STATUS "Found HRPSYS_BASE in ${HRPSYS_BASE_DIR}")
  endif()
else()
  if(NOT HRPSYS_BASE_FIND_QUIETLY)
    if(HRPSYS_BASE_FIND_REQUIRED)
      message(FATAL_ERROR "HRPSYS_BASE required, please specify it's location.")
    endif()
  endif()
endif()
