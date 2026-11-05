get_property(translation_libs GLOBAL PROPERTY MLIR_TRANSLATION_LIBS)

include_directories(${LLVM_MAIN_SRC_DIR}/../mlir/include)
include_directories(${CMAKE_BINARY_DIR}/tools/mlir/include)

set(LLVM_LINK_COMPONENTS
  AllTargetsAsmParsers
  AllTargetsDescs
  AllTargetsInfos
  Object
  Support
  )

add_clang_tool(cir-link
  cir-link.cpp
  )

clang_target_link_libraries(cir-link
  PRIVATE
  clangCIR
  clangCIRLoweringDirectToLLVM
  MLIRCIR
  )

target_link_libraries(cir-link
  PRIVATE
  MLIRBytecodeReader
  MLIRBytecodeWriter
  MLIRDialect
  MLIRDLTIDialect
  MLIRFuncDialect
  MLIRIR
  MLIRLLVMDialect
  MLIRParser
  MLIRSupport
  ${translation_libs}
  )
