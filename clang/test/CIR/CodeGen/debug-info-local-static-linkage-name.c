// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s --check-prefix=OGCG

// A function-local static variable must not carry a DW_AT_linkage_name: classic
// CodeGen omits the mangled symbol name for function-scoped statics (it is not
// useful for debugger lookup of a function-local). CIR previously attached the
// lowered mangled name (e.g. "counter.n") as the linkage name.

int counter(void) {
  static int n = 0;
  return ++n;
}

// The DIGlobalVariable has scope right after name, i.e. no linkageName between
// them (a linkageName would print as `name: "n", linkageName: "...", scope:`).
// CHECK: !DIGlobalVariable(name: "n", scope: ![[#]]
// OGCG: !DIGlobalVariable(name: "n", scope: ![[#]]
