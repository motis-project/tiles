cmake_minimum_required(VERSION 3.15)
project(tiles)

if (MSVC)
  set(TILES_WARNINGS "/W4")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  set(TILES_WARNINGS "-Wall" "-Wextra")
endif()

include(cmake/pkg.cmake)

file(GLOB_RECURSE tiles-server-res "${CMAKE_CURRENT_SOURCE_DIR}/client/*.*")
create_resource("${CMAKE_CURRENT_SOURCE_DIR}/client/" "${tiles-server-res}" tiles_server_res)

include(cmake/clang-tidy.cmake)

file(GLOB_RECURSE tiles-files
  src/db/*.cc
  src/feature/*.cc
  src/fixed/*.cc
  src/mvt/*.cc
  src/tile_database.cc
  src/perf_counter.cc
  src/util.cc
)

add_library(tiles STATIC ${tiles-files})
set_property(TARGET tiles PROPERTY CXX_STANDARD 17)
target_compile_options(tiles PRIVATE ${TILES_WARNINGS})
target_include_directories(tiles PUBLIC include)
target_link_libraries(tiles
  boost
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
  mpark_variant
)

file(GLOB_RECURSE tiles-import-files src/osm/*.cc)

add_library(tiles-import-library EXCLUDE_FROM_ALL
  ${tiles-import-files})
set_property(TARGET tiles-import-library PROPERTY CXX_STANDARD 17)
target_compile_options(tiles-import-library PRIVATE ${TILES_WARNINGS})
target_include_directories(tiles-import-library PUBLIC include)
target_link_libraries(tiles-import-library
  boost-filesystem
  geo
  lmdb
  lua
  tiles
)

file(GLOB tiles-import-main-files src/import.cc)

add_executable(tiles-import EXCLUDE_FROM_ALL
  ${tiles-import-main-files})
set_property(TARGET tiles-import PROPERTY CXX_STANDARD 17)
target_compile_options(tiles-import PRIVATE ${TILES_WARNINGS})
target_include_directories(tiles-import PUBLIC include)
target_link_libraries(tiles-import
  boost
  conf
  lmdb
  tiles-import-library
)

add_executable(tiles-server EXCLUDE_FROM_ALL src/server.cc)
set_property(TARGET tiles-server PROPERTY CXX_STANDARD 17)
target_compile_definitions(tiles-server PRIVATE BOOST_BEAST_USE_STD_STRING_VIEW=1)
target_compile_options(tiles-server PRIVATE ${TILES_WARNINGS})
target_include_directories(tiles-server PUBLIC include)
target_link_libraries(tiles-server
  boost-filesystem
  conf
  geo
  lmdb
  tiles
)

target_link_libraries(tiles-server
  tiles_server_res
  tiles_server_res-res
)

target_link_libraries(tiles-server
  pbf_sdf_fonts_res
  pbf_sdf_fonts_res-res
)

file(GLOB_RECURSE tiles-benchmark-files src/benchmark.cc)

add_executable(tiles-benchmark EXCLUDE_FROM_ALL ${tiles-benchmark-files})
set_property(TARGET tiles-benchmark PROPERTY CXX_STANDARD 17)
target_compile_options(tiles-benchmark PRIVATE ${TILES_WARNINGS})
target_include_directories(tiles-benchmark PUBLIC include)
target_link_libraries(tiles-benchmark
  boost
  tiles
  lmdb
  geo
)

file(GLOB_RECURSE tiles-test-files
  test/catch_main.cc
  test/*_test.cc
  src/osm/hybrid_node_idx.cc
)

add_executable(tiles-test EXCLUDE_FROM_ALL ${tiles-test-files})
set_property(TARGET tiles-test PROPERTY CXX_STANDARD 17)
target_compile_options(tiles-test PRIVATE ${TILES_WARNINGS})
target_include_directories(tiles-test PUBLIC include)
target_link_libraries(tiles-test tiles Catch2)


# --- fuzzing
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  function(tiles_add_fuzz_target target_name filename)
    file(GLOB_RECURSE files ${filename})
    add_executable(${target_name} EXCLUDE_FROM_ALL ${files})
    set_property(TARGET ${target_name} PROPERTY CXX_STANDARD 17)
    target_compile_options(${target_name} PRIVATE -fsanitize=address,fuzzer)
    target_include_directories(${target_name} PUBLIC include)
    target_link_libraries(${target_name} tiles -fsanitize=address,fuzzer)
  endfunction(tiles_add_fuzz_target)

  tiles_add_fuzz_target(tiles-fuzz-repack-features test/repack_features_fuzz.cc)
  tiles_add_fuzz_target(tiles-fuzz-feature-pack test/feature_pack_fuzz.cc)
endif()
