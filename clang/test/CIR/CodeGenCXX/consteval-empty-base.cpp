// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Verify no spurious undef stores for consteval constructors of
// empty structs used as overlapping base classes or [[no_unique_address]]
// members.

struct C { char c = 1; };
struct Empty { consteval Empty() {} };
struct Empty2 { consteval Empty2() {} };
struct Test : C, Empty {
  [[no_unique_address]] Empty2 e;
};
static_assert(sizeof(Test) == 1);
void f() { Test test; }

// LLVM-LABEL: define {{.*}} @_ZN4TestC2Ev
// LLVM:         call void @_ZN1CC2Ev
// LLVM-NOT:     store {{.*}} undef
// LLVM-NEXT:    ret void

// OGCG-LABEL: define {{.*}} @_ZN4TestC2Ev
// OGCG:         call void @_ZN1CC2Ev
// OGCG-NOT:     store {{.*}} undef
// OGCG-NEXT:    ret void
