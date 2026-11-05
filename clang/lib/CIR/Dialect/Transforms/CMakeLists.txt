add_subdirectory(TargetLowering)

add_clang_library(MLIRCIRTransforms
  ABILowering.cpp
  CallConvLowering.cpp
  CIRCanonicalize.cpp
  CIRSimplify.cpp
  CXXABILowering.cpp
  FlattenCFG.cpp
  HoistAllocas.cpp
  IdiomRecognizer.cpp
  LibOpt.cpp
  LifetimeCheck.cpp
  LoweringPrepare.cpp
  LoweringPrepareAArch64CXXABI.cpp
  LoweringPrepareARMCXXABI.cpp
  LoweringPrepareItaniumCXXABI.cpp
  GotoSolver.cpp
  PointsToAnalysis.cpp
  LiveObjectAnalysis.cpp
  MoveOpt.cpp
  SCFPrepare.cpp
  StdHelpers.cpp
  StdLibStatistics.cpp
  TailMerge.cpp
  CIRInliner.cpp

  DEPENDS
  MLIRCIRPassIncGen

  LINK_LIBS PUBLIC
  clangAST
  clangBasic
  clangFrontend

  MLIRAnalysis
  MLIRIR
  MLIRPass
  MLIRTransformUtils
  MLIRLLVMIRTransforms
  MLIRFuncInlinerExtension
  MLIRSideEffectInterfaces

  MLIRCIR
  MLIRCIRInterfaces
  MLIRCIRTargetLowering
)
