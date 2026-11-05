set(LLVM_LINK_COMPONENTS
  Object
  Support
  )

add_clang_tool(cir-link-wrapper
  cir-link-wrapper.cpp
  )
