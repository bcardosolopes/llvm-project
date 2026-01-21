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
  LoweringPrepareItaniumCXXABI.cpp
  GotoSolver.cpp
  PointsToAnalysis.cpp
  LiveObjectAnalysis.cpp
  MoveOpt.cpp
  SCFPrepare.cpp
  StdHelpers.cpp
  StdLibStatistics.cpp

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
  MLIRSideEffectInterfaces

  MLIRCIR
  MLIRCIRInterfaces
  MLIRCIRTargetLowering
)
