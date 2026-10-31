cmake_minimum_required(VERSION 3.20)
project(haunted_hayride VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(HAYRIDE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(HAYRIDE_BUILD_TESTS "Build the test suite" ON)
option(HAYRIDE_WEB "Build the browser game (Emscripten + Strata's WebGL 2 backend)" OFF)

# Strata compiles C++ exceptions as wasm exceptions, and everything in one
# .wasm must match.
if(EMSCRIPTEN)
  cmake_minimum_required(VERSION 3.24)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions>")
  add_link_options(-fwasm-exceptions -sALLOW_MEMORY_GROWTH=1)
endif()
if(HAYRIDE_WEB AND NOT EMSCRIPTEN)
  message(FATAL_ERROR "HAYRIDE_WEB needs Emscripten (emcmake cmake ...)")
endif()

function(hayride_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(HAYRIDE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(HAYRIDE_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# The ride: the road, the hiding places, the spooks and the flashlight. No
# engine and no GPU, so it builds and tests in seconds. Never fuse
# multiply-adds: the same ride on every compiler.
add_library(hayride_ride STATIC src/ride/route.cpp src/ride/ride.cpp)
target_include_directories(hayride_ride PUBLIC src)
hayride_warnings(hayride_ride)
if(MSVC)
  target_compile_options(hayride_ride PRIVATE /fp:precise)
else()
  target_compile_options(hayride_ride PRIVATE -ffp-contract=off)
endif()

if(HAYRIDE_BUILD_TESTS)
  enable_testing()
  add_executable(test_ride tests/test_ride.cpp)
  target_link_libraries(test_ride PRIVATE hayride_ride)
  hayride_warnings(test_ride)
  add_test(NAME ride COMMAND test_ride)
endif()

# ---------------------------------------------------------------- the game on Strata
# Strata is fetched at HAYRIDE_STRATA_REF (the v0.7.0 tag: ABI 1.12), or taken
# from a local checkout with -DFETCHCONTENT_SOURCE_DIR_STRATA=<path>.
if(HAYRIDE_WEB)
  include(FetchContent)
  set(HAYRIDE_STRATA_REF "20827c3d67e71c1f942c29e7cf8adfa4801188a8" CACHE STRING "Strata git ref (a full commit SHA)")
  set(STRATA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(STRATA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(STRATA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(STRATA_WITH_SIM OFF CACHE BOOL "" FORCE)
  set(STRATA_WITH_USD OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(strata
    GIT_REPOSITORY https://github.com/Preponderous-Software/strata.git
    GIT_TAG ${HAYRIDE_STRATA_REF})
  FetchContent_MakeAvailable(strata)

  add_library(hayride_view STATIC src/view/mesh.cpp src/view/scene.cpp)
  target_include_directories(hayride_view PUBLIC src)
  target_link_libraries(hayride_view PUBLIC hayride_ride strata::static)
  hayride_warnings(hayride_view)

  # emcmake cmake -S . -B build-web -DHAYRIDE_WEB=ON: the page is
  # haunted-hayride.html + .js + .wasm.
  add_executable(hayride-web app/web_main.cpp)
  target_link_libraries(hayride-web PRIVATE hayride_view)
  hayride_warnings(hayride-web)
  set_target_properties(hayride-web PROPERTIES OUTPUT_NAME haunted-hayride SUFFIX ".html")
  target_link_options(hayride-web PRIVATE
    -sSTACK_SIZE=1MB
    "-sEXPORTED_FUNCTIONS=_main,_hayride_aim,_hayride_start,_hayride_title,_hayride_pause,_hayride_auto,_hayride_speed,_hayride_resize,_hayride_night,_hayride_rides,_hayride_status"
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString
    "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
  # The loading shell (web/strata-kit, vendored from Strata) and its bridge go
  # inline into the page, with the download's size (web/strata_page.cmake).
  set_property(TARGET hayride-web APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/strata-kit/strata-loader.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/web/strata-boot.js")
  add_custom_command(TARGET hayride-web POST_BUILD
    COMMAND "${CMAKE_COMMAND}" "-DHTML=$<TARGET_FILE:hayride-web>"
            "-DWASM=$<TARGET_FILE_DIR:hayride-web>/$<TARGET_FILE_BASE_NAME:hayride-web>.wasm"
            "-DWEB=${CMAKE_CURRENT_SOURCE_DIR}/web" -P "${CMAKE_CURRENT_SOURCE_DIR}/web/strata_page.cmake"
    VERBATIM)
endif()
