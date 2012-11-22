cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake_modules")

set(RTC_NAMES Axis282 Axis241Q Axis212)
set(idl "Img.idl")

# check OpenRTM installed
find_package(OpenRTM REQUIRED)

list(APPEND CFLAGS ${OPENRTM_CFLAGS})
list(APPEND INCLUDE_DIRS ${OPENRTM_INCLUDE_DIRS})
list(APPEND LDFLAGS ${OPENRTM_LDFLAGS})
list(APPEND LIBRARY_DIRS ${OPENRTM_LIBRARY_DIRS})
list(APPEND LIBRARIES ${OPENRTM_LIBRARIES})

list(APPEND INCLUDE_DIRS ${HRPSYS_BASE_INCLUDE_DIRS})
list(APPEND LIBRARY_DIRS ${HRPSYS_BASE_LIBRARY_DIRS})
list(APPEND LIBRARIES microhttpd jpeg opencv_core opencv_imgproc)

include_directories(${INCLUDE_DIRS})
link_directories(${LIBRARY_DIRS})
add_definitions(${CFLAGS})
add_definitions(-I${CMAKE_CURRENT_BINARY_DIR})


string(REGEX REPLACE "([^\\/]*)\\.idl" "\\1" idl_base ${idl})
set(idl_flags ${VERBOSE_FLAG} ${OPENRTM_IDLFLAGS} -Wbh=.hh -Wbs=Sk.cpp -Wba -Wbd=DynSk.cpp)
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${idl_base}.hh ${CMAKE_CURRENT_BINARY_DIR}/${idl_base}Sk.cpp ${CMAKE_CURRENT_BINARY_DIR}/${idl_base}DynSk.cpp
    COMMAND ${OPENRTM_IDLC} ${idl_flags} ${PROJECT_SOURCE_DIR}/${idl}
    DEPENDS ${idl}
)

foreach(rtc_name ${RTC_NAMES})
  set(LIBS_SRCS ${rtc_name}.cpp JpegCompress.cpp ${idl_base}Sk.cpp ${idl_base}DynSk.cpp)
  set(LIBS_INCS ${rtc_name}.h JpegCompress.h ${idl_base}.hh)
  set(EXEC_SRCS ${rtc_name}Comp.cpp )

  add_library(${rtc_name} SHARED ${LIBS_SRCS})
  target_link_libraries(${rtc_name} ${LIBRARIES})
  set_target_properties(${rtc_name} PROPERTIES PREFIX "")
  set_target_properties(${rtc_name} PROPERTIES LINK_FLAGS "${LDFLAGS}")

  add_library(${rtc_name}-static STATIC ${LIBS_SRCS} )
  target_link_libraries(${rtc_name}-static ${LIBRARIES})
  set_target_properties(${rtc_name}-static PROPERTIES PREFIX "")
  set_target_properties(${rtc_name}-static PROPERTIES LINK_FLAGS "${LDFLAGS}")

  add_executable(${rtc_name}Comp ${EXEC_SRCS})
  add_dependencies(${rtc_name}Comp ${rtc_name}-static)
  target_link_libraries(${rtc_name}Comp ${rtc_name}-static ${LIBRARIES})

  add_executable(${rtc_name}Comp-dynamic ${EXEC_SRCS})
  add_dependencies(${rtc_name}Comp-dynamic ${rtc_name})
  target_link_libraries(${rtc_name}Comp-dynamic ${rtc_name} ${LIBRARIES})
  set_target_properties(${rtc_name}Comp-dynamic PROPERTIES LINK_FLAGS "${LDFLAGS}")

endforeach()
