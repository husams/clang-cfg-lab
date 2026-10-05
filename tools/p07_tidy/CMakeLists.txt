# clang-tidy plugin module. Homebrew ships clang-tidy's libraries only as static
# archives, so the module is NOT linked against them: its clang-tidy symbols stay
# undefined and are resolved from the clang-tidy executable when it dlopen()s us
# (clang-tidy -load=...). Everything else comes from the same two dylibs.
add_library(p07_tidy MODULE UseAfterMoveDataflowCheck.cpp)
target_link_libraries(p07_tidy PRIVATE clang-cpp LLVM)
target_link_options(p07_tidy PRIVATE -undefined dynamic_lookup)
set_target_properties(p07_tidy PROPERTIES
  PREFIX "" SUFFIX ".dylib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  BUILD_RPATH "${LLVM_INSTALL_PREFIX}/lib")
