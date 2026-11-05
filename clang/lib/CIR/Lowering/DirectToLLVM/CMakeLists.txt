set(LLVM_LINK_COMPONENTS
  Core
  ProfileData
  Support
  )

get_property(dialect_libs GLOBAL PROPERTY MLIR_DIALECT_LIBS)
get_property(translation_libs GLOBAL PROPERTY MLIR_TRANSLATION_LIBS)

add_clang_library(clangCIRLoweringDirectToLLVM
  CIROpenMPHelpers.cpp
  CIRToLLVMIR.cpp
  LowerTBAAToLLVM.cpp
  LowerToLLVM.cpp
  LowerToLLVMIR.cpp

  DEPENDS
  MLIRCIREnumsGen
  MLIRCIROpsIncGen
  MLIRCIROpInterfacesIncGen

  LINK_LIBS
  clangCIRLoweringCommon
  ${dialect_libs}
  MLIRCIR
  MLIRCIRTargetLowering
  MLIRLLVMIRTransforms
  ${translation_libs}
  MLIROpenMPDialect
  MLIROpenMPTransforms
  MLIROpenMPToLLVM
  MLIRIR
  )

target_include_directories(clangCIRLoweringDirectToLLVM PRIVATE
  ${CLANG_SOURCE_DIR}/lib/CIR/Dialect/Transforms/TargetLowering
  )
