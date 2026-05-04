cmake_minimum_required(VERSION 3.19)
project(lite-xl-ghostty C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(LXL_GHOSTTY_BUILD_NATIVE "Build the Lite XL native Ghostty module" ON)
option(LXL_GHOSTTY_BUILD_TESTS "Build unit tests" ON)

set(GHOSTTY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/references/ghostty"
    CACHE PATH "Ghostty source checkout")
set(LITE_XL_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/lite-xl/resources/include"
    CACHE PATH "Lite XL plugin API include directory")

find_package(Threads REQUIRED)

set(LXL_GHOSTTY_SUPPORT_SOURCES
  src/event_queue.c
  src/osc_observer.c
  src/utf8.c
)

add_library(lxl_ghostty_support STATIC ${LXL_GHOSTTY_SUPPORT_SOURCES})
target_include_directories(lxl_ghostty_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(lxl_ghostty_support PUBLIC Threads::Threads)

if(LXL_GHOSTTY_BUILD_NATIVE)
  if(EXISTS "${GHOSTTY_SOURCE_DIR}/CMakeLists.txt")
    add_subdirectory("${GHOSTTY_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/ghostty")
  else()
    message(FATAL_ERROR "Ghostty source checkout not found at ${GHOSTTY_SOURCE_DIR}")
  endif()

  add_library(ghostty_lxl MODULE
    src/lxl_ghostty.c
    src/pty_unix.c
    src/terminal.c
  )
  set_target_properties(ghostty_lxl PROPERTIES
    PREFIX ""
    OUTPUT_NAME "init"
  )
  if(APPLE)
    set_target_properties(ghostty_lxl PROPERTIES SUFFIX ".lib")
  else()
    set_target_properties(ghostty_lxl PROPERTIES SUFFIX ".so")
  endif()
  target_include_directories(ghostty_lxl PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${GHOSTTY_SOURCE_DIR}/include"
    "${LITE_XL_INCLUDE_DIR}"
  )
  target_link_libraries(ghostty_lxl PRIVATE lxl_ghostty_support ghostty-vt-static)
  if(UNIX AND NOT APPLE)
    target_link_libraries(ghostty_lxl PRIVATE util)
  endif()
  add_custom_command(TARGET ghostty_lxl POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/libraries/ghostty_lxl"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:ghostty_lxl>"
            "${CMAKE_CURRENT_SOURCE_DIR}/libraries/ghostty_lxl/$<TARGET_FILE_NAME:ghostty_lxl>")

  if(LXL_GHOSTTY_BUILD_TESTS)
    add_executable(test_dlopen tests/test_dlopen.c)
    add_test(NAME native_module_load COMMAND test_dlopen "$<TARGET_FILE:ghostty_lxl>")
  endif()
endif()

if(LXL_GHOSTTY_BUILD_TESTS)
  enable_testing()
  add_executable(test_event_queue tests/test_event_queue.c)
  target_link_libraries(test_event_queue PRIVATE lxl_ghostty_support)
  add_test(NAME event_queue COMMAND test_event_queue)

  add_executable(test_osc_observer tests/test_osc_observer.c)
  target_link_libraries(test_osc_observer PRIVATE lxl_ghostty_support)
  add_test(NAME osc_observer COMMAND test_osc_observer)

  add_executable(test_utf8 tests/test_utf8.c)
  target_link_libraries(test_utf8 PRIVATE lxl_ghostty_support)
  add_test(NAME utf8 COMMAND test_utf8)

  find_program(LUA_EXECUTABLE NAMES lua lua5.4 luajit)
  if(LUA_EXECUTABLE)
    add_test(NAME lua_helpers
      COMMAND "${LUA_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_lua_helpers.lua")
    set_tests_properties(lua_helpers PROPERTIES
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()
