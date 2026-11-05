// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fms-extensions -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fms-extensions -emit-llvm -o %t-ogcg.ll %s
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// CIR was crashing with "isInvokeDest: no exceptions or ignore
// exception" when -fms-extensions was used without -fexceptions on an Itanium
// ABI target. The fix returns false (no invoke dest) when the function does
// not use SEH try, matching OGCG's behavior.

void fa(__unaligned struct A *) {}
// LLVM: define {{(dso_local )?}}void @_Z2faPU11__unaligned1A(
// OGCG: define {{(dso_local )?}}void @_Z2faPU11__unaligned1A(

void ga(struct A *, struct A *) {}
// LLVM: define {{(dso_local )?}}void @_Z2gaP1AS0_(
// OGCG: define {{(dso_local )?}}void @_Z2gaP1AS0_(

struct A;
void memptr(void (A::*a)(int) __unaligned) {}
// LLVM: define {{.*}} @_Z6memptrM1AU11__unalignedFviE(
// OGCG: define {{.*}} @_Z6memptrM1AU11__unalignedFviE(
