# Shared build for every tool in the clang-cfg-lab.
#
# Configure & build everything (from the lab root):
#   scripts/build.sh                 # all tools
#   scripts/build.sh p02_walk        # just one
#
# Adding a tool needs NO edit here: create tools/pNN_<name>/ containing one
# or more .cpp files and re-run scripts/build.sh. Every such directory becomes
# an executable called pNN_<name> in build/bin/.
#
# A directory may carry its own CMakeLists.txt to override the default; if it
# does, it should call add_cfg_tool(<name> <sources...>) itself.

cmake_minimum_required(VERSION 3.20)
# C is enabled too: LLVM's CMake modules run C header probes (FindLibEdit).
project(clang_cfg_lab C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")


find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)
message(STATUS "clang-cfg-lab: LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")
if(NOT LLVM_VERSION_MAJOR EQUAL 22)
  message(WARNING "clang-cfg-lab was written against LLVM 22; found ${LLVM_PACKAGE_VERSION}. "
                  "Expect API drift.")
endif()

include_directories(SYSTEM ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/common")

# Paths baked into cfglab.h (addPlatformFlags). LLVM_INSTALL_PREFIX is the
# real (versioned Cellar) path; scripts/build.sh re-runs CMake on every call,
# so a `brew upgrade llvm` is picked up on the next build.
set(CFGLAB_LLVM_PREFIX "${LLVM_INSTALL_PREFIX}")
set(CFGLAB_RESOURCE_DIR "${LLVM_INSTALL_PREFIX}/lib/clang/${LLVM_VERSION_MAJOR}")
message(STATUS "clang-cfg-lab: resource dir ${CFGLAB_RESOURCE_DIR}")

# LLVM's dylibs are built without RTTI; tools must match or link fails.
if(NOT LLVM_ENABLE_RTTI)
  add_compile_options(-fno-rtti)
endif()
add_compile_options(-Wall -Wno-unused-parameter)

# add_cfg_tool(<name> <source>...)
#   One executable linked against the monolithic libclang-cpp and libLLVM
#   dylibs. Output: build/bin/<name>.
function(add_cfg_tool name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE clang-cpp LLVM)
  target_compile_definitions(${name} PRIVATE
    CFGLAB_LLVM_PREFIX="${CFGLAB_LLVM_PREFIX}"
    CFGLAB_RESOURCE_DIR="${CFGLAB_RESOURCE_DIR}")
  # Run the dylibs from the Homebrew prefix without DYLD_* variables.
  set_target_properties(${name} PROPERTIES
    BUILD_RPATH "${LLVM_INSTALL_PREFIX}/lib"
    INSTALL_RPATH "${LLVM_INSTALL_PREFIX}/lib")
endfunction()

# Auto-discover tools/pNN_<name>/ directories.
file(GLOB _tool_dirs LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/p[0-9][0-9]_*")
list(SORT _tool_dirs)
foreach(dir ${_tool_dirs})
  if(NOT IS_DIRECTORY "${dir}")
    continue()
  endif()
  get_filename_component(_name "${dir}" NAME)
  if(EXISTS "${dir}/CMakeLists.txt")
    add_subdirectory("${dir}" "${CMAKE_CURRENT_BINARY_DIR}/${_name}")
  else()
    file(GLOB _srcs "${dir}/*.cpp")
    list(SORT _srcs)
    if(_srcs)
      add_cfg_tool(${_name} ${_srcs})
    endif()
  endif()
endforeach()
