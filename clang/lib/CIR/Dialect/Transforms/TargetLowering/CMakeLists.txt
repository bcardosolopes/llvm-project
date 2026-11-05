add_clang_library(MLIRCIRTargetLowering
  ABIInfo.cpp
  ABIInfoImpl.cpp
  CIRCXXABI.cpp
  CIRLowerContext.cpp
  CIRRecordLayout.cpp
  ItaniumCXXABI.cpp
  LowerCall.cpp
  LowerFunction.cpp
  LowerModule.cpp
  MicrosoftCXXABI.cpp
  LoweringPrepareX86CXXABI.cpp
  LowerTypes.cpp
  RecordLayoutBuilder.cpp
  SwiftCallingConv.cpp
  TargetInfo.cpp
  TargetLoweringInfo.cpp
  Targets/AArch64.cpp
  Targets/ARM.cpp
  Targets/AMDGPU.cpp
  Targets/BPF.cpp
  Targets/NVPTX.cpp
  Targets/RISCV.cpp
  Targets/SPIR.cpp
  Targets/WebAssembly.cpp
  Targets/X86.cpp

  DEPENDS
  clangBasic

  LINK_COMPONENTS
  TargetParser

  LINK_LIBS PUBLIC

  clangBasic
  MLIRIR
  MLIRPass
  MLIRDLTIDialect
  MLIRCIR
  MLIRCIRInterfaces
  MLIRLLVMDialect
)
