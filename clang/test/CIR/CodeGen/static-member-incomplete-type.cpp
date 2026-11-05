// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm \
// RUN:   -debug-info-kind=constructor -std=c++20 -w %s -o %t.ll
// RUN: FileCheck %s < %t.ll

// A static data member of an incomplete template specialization type.
// The member is declared but never defined in this TU, but referenced
// to force debug info emission. The canonical declaration's type desugars
// to an incomplete record (forward-declared vector) — getTypeAlign must
// not be called on it during debug info rewriting.
namespace ns {
template<class T, class A = void> class vector;
}

struct ShaderInfo {
  const char *name;
  static ns::vector<int> uniformBlocks;
};

extern ns::vector<int> *getBlocks();
ns::vector<int> *getBlocks() { return &ShaderInfo::uniformBlocks; }

// CHECK: @_ZN10ShaderInfo13uniformBlocksE = external global
// CHECK: !DIGlobalVariable(name: "uniformBlocks"
// CHECK: !DIDerivedType(tag: DW_TAG_member, name: "uniformBlocks"
