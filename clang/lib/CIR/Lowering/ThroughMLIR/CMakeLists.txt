set(LLVM_LINK_COMPONENTS
  Core
  Support
  )

get_property(dialect_libs GLOBAL PROPERTY MLIR_DIALECT_LIBS)

add_clang_library(clangCIRLoweringThroughMLIR
  LowerCIRLoopToSCF.cpp
  LowerCIRToMLIR.cpp
  LowerMLIRToLLVM.cpp

  DEPENDS
  MLIRCIROpsIncGen
  MLIRCIREnumsGen
  MLIRCIROpInterfacesIncGen
  MLIRCIRLoopOpInterfaceIncGen

  LINK_LIBS
  clangAST
  clangBasic
  clangCIR
  clangCIRLoweringCommon
  ${dialect_libs}
  MLIRCIR
  MLIRAnalysis
  MLIRBuiltinToLLVMIRTranslation
  MLIRLLVMToLLVMIRTranslation
  MLIRIR
  MLIRParser
  MLIRSideEffectInterfaces
  MLIRTransforms
  MLIRSupport
  MLIRMemRefDialect
  MLIROpenMPDialect
  MLIROpenMPToLLVMIRTranslation
  )
