set(MLIR_INCLUDE_DIR ${LLVM_MAIN_SRC_DIR}/../mlir/include )
set(MLIR_TABLEGEN_OUTPUT_DIR ${CMAKE_BINARY_DIR}/tools/mlir/include)
include_directories(SYSTEM ${MLIR_INCLUDE_DIR})
include_directories(${MLIR_TABLEGEN_OUTPUT_DIR})

add_distinct_clang_unittest(CIRUnitTests
  CIRGenModuleTest.cpp
  ConstPtrAttrTest.cpp
  GlobalViewDiffAttrTest.cpp
  MissingFeaturesTest.cpp
  PointerLikeTest.cpp
  CLANG_LIBS
  clangAST
  clangBasic
  clangCIR
  clangFrontend
  clangLex
  clangSerialization
  clangTooling
  LLVM_COMPONENTS
  Core

  LINK_LIBS
  MLIRCIR
  CIROpenACCSupport
  MLIRIR
  MLIROpenACCDialect
  )
