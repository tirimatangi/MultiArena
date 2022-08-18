cmake_minimum_required(VERSION 3.14)

project(MultiArenaExamples CXX)

include(FetchContent)
FetchContent_Declare(MultiArena SOURCE_DIR "${PROJECT_SOURCE_DIR}/..")
FetchContent_MakeAvailable(MultiArena)

foreach(name IN ITEMS example-1 example-2 example-3)
  add_executable("${name}" "${name}.cc")
  target_link_libraries("${name}" PRIVATE MultiArena::MultiArena)
  target_compile_features("${name}" PRIVATE cxx_std_17)
endforeach()
