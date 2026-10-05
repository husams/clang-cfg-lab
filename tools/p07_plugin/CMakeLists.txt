# A plugin is a loadable module, not an executable: build it as a MODULE and put it in
# build/lib/. It links the same two dylibs as the compiler, so the compiler and the
# plugin share ONE copy of Clang's registries.
add_library(p07_plugin MODULE plugin.cpp)
target_link_libraries(p07_plugin PRIVATE clang-cpp LLVM)
set_target_properties(p07_plugin PROPERTIES
  PREFIX "" SUFFIX ".dylib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  BUILD_RPATH "${LLVM_INSTALL_PREFIX}/lib")
