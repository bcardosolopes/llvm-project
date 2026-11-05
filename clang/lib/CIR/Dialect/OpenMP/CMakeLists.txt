add_clang_library(CIROpenMPSupport
  RegisterOpenMPExtensions.cpp

  DEPENDS
  MLIRCIRTypeConstraintsIncGen

  LINK_LIBS PUBLIC
  MLIRIR
  MLIRCIR
  MLIROpenMPDialect
  )
