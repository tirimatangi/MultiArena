cmake_minimum_required(VERSION 3.14)

project(MultiArena
        VERSION 0.0.1
        DESCRIPTION "Header-only library for a polymorphic allocator with constant allocation and deallocation times."
        HOMEPAGE_URL "https://github.com/tirimatangi/MultiArena"
        LANGUAGES CXX)

# ---- Warning guard ----

# Protect dependents from this project's warnings if the guard isn't disabled
set(MultiArena_warning_guard SYSTEM)
if(MultiArena_INCLUDE_WITHOUT_SYSTEM)
  set(MultiArena_warning_guard "")
endif()

# ---- Declare library ----
# Use release build by default.
if (NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

# Defining MULTIARENA_DISABLE_EXCEPTIONS disables exceptions
# set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -DMULTIARENA_DISABLE_EXCEPTIONS")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-Wall -ftemplate-backtrace-limit=0 -DDEBUG -g -DMULTIARENA_DEBUG")

if(${CMAKE_BUILD_TYPE} MATCHES "Release")
  message("CMAKE_CXX_FLAGS_RELEASE is ${CMAKE_CXX_FLAGS_RELEASE}")
endif()

if(${CMAKE_BUILD_TYPE} MATCHES "Debug")
  message("CMAKE_CXX_FLAGS_DEBUG is ${CMAKE_CXX_FLAGS_DEBUG}")
endif()

add_library(MultiArena INTERFACE)
add_library(MultiArena::MultiArena ALIAS MultiArena)

target_include_directories(MultiArena
        ${MultiArena_warning_guard}
        INTERFACE
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>")

target_compile_features(MultiArena INTERFACE cxx_std_17)

# ---- Install ----

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(MultiArena_directory "MultiArena-${PROJECT_VERSION}")
set(MultiArena_include_directory
        "${CMAKE_INSTALL_INCLUDEDIR}/${MultiArena_directory}")

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
        DESTINATION "${MultiArena_include_directory}")

install(TARGETS MultiArena
        EXPORT MultiArenaTargets
        INCLUDES DESTINATION "${MultiArena_include_directory}")

write_basic_package_version_file(
        MultiArenaConfigVersion.cmake
        COMPATIBILITY SameMajorVersion
        ARCH_INDEPENDENT)

set(MultiArena_install_cmakedir
        "${CMAKE_INSTALL_LIBDIR}/cmake/${MultiArena_directory}")

install(FILES
        "${PROJECT_SOURCE_DIR}/cmake/MultiArenaConfig.cmake"
        "${PROJECT_BINARY_DIR}/MultiArenaConfigVersion.cmake"
        DESTINATION "${MultiArena_install_cmakedir}")

install(EXPORT MultiArenaTargets
        NAMESPACE MultiArena::
        DESTINATION "${MultiArena_install_cmakedir}")
