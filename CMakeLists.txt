cmake_minimum_required(VERSION 3.0)
project(tiles)

find_package(Boost REQUIRED COMPONENTS system filesystem)
find_package(Threads)

link_directories(${Boost_LIBRARY_DIRS})
include_directories(SYSTEM ${Boost_INCLUDE_DIR})

if (MSVC)
  set(TILES_CXX_FLAGS "-std=c++17 -Wall")
else()
  set(TILES_CXX_FLAGS "-std=c++17 -Wall -Wextra")
endif()

include(cmake/pkg.cmake)

file(GLOB_RECURSE tiles-files
  src/db/*.cc
  src/fixed/*.cc
  src/mvt/*.cc
  src/tile_database.cc
  src/perf_counter.cc
  src/util.cc
)

add_library(tiles STATIC ${tiles-files})
set_target_properties(tiles PROPERTIES COMPILE_FLAGS ${TILES_CXX_FLAGS})
target_include_directories(tiles PUBLIC include)
target_link_libraries(tiles
  ${Boost_LIBRARIES}
  clipper
  fmt
  geo
  lmdb
  osmium
  utl
  concurrentqueue
  conf
  protozero
  sol2
  miniz
  zlibstatic
)

file(GLOB_RECURSE tiles-import-files src/osm/*.cc)
file(GLOB tiles-import-main-files src/import.cc)

add_executable(tiles-import EXCLUDE_FROM_ALL
  ${tiles-import-main-files}
  ${tiles-import-files})
set_target_properties(tiles-import PROPERTIES COMPILE_FLAGS ${TILES_CXX_FLAGS})
target_include_directories(tiles-import PUBLIC include)
target_link_libraries(tiles-import
  ${Boost_LIBRARIES}
  conf
  geo
  lmdb
  lua
  tiles
)

file(GLOB_RECURSE tiles-server-files src/server.cc)

add_executable(tiles-server EXCLUDE_FROM_ALL ${tiles-server-files})
set_target_properties(tiles-server PROPERTIES COMPILE_FLAGS ${TILES_CXX_FLAGS})
target_include_directories(tiles-server PUBLIC include)
target_link_libraries(tiles-server
  ${Boost_LIBRARIES}
  geo
  lmdb
  net-http_server
  tiles
)


file(GLOB_RECURSE tiles-benchmark-files src/benchmark.cc)

add_executable(tiles-benchmark EXCLUDE_FROM_ALL ${tiles-benchmark-files})
set_target_properties(tiles-benchmark PROPERTIES COMPILE_FLAGS ${TILES_CXX_FLAGS})
target_include_directories(tiles-benchmark PUBLIC include)
target_link_libraries(tiles-benchmark
  ${Boost_LIBRARIES}
  tiles
  lmdb
  geo
)

file(GLOB_RECURSE tiles-test-files test/*.cc)

add_executable(tiles-test EXCLUDE_FROM_ALL ${tiles-test-files})
set_target_properties(tiles-test PROPERTIES COMPILE_FLAGS ${TILES_CXX_FLAGS})
target_include_directories(tiles-test PUBLIC include)
target_link_libraries(tiles-test tiles)
