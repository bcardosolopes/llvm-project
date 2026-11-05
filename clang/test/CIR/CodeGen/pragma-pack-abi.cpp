// RUN: %clang_cc1 -triple x86_64-apple-macosx10.7.2 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-apple-macosx10.7.2 -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// Packed inherited struct ABI - CIR must coerce to correct integer width

struct FOO {
  unsigned int x;
};

#pragma pack(push, 2)
struct BAR : FOO {
  char y;
};
#pragma pack(pop)

BAR f(BAR x) {
  return x;
}

// LLVM: define i48 @_Z1f3BAR(i48 %{{.*}})
// OGCG: define i48 @_Z1f3BAR(i48 %{{.*}})
