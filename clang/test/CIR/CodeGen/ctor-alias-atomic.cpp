// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-mlir=cir-abi -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -mconstructor-aliases -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

struct Z {
  char c;
};

struct B {
  B(_Atomic Z);
};

B::B(_Atomic Z) {}

// CIR: cir.func{{.*}} @_ZN1BC2EU7_Atomic1Z(%arg0: !cir.ptr<!rec_B>
// CIR: cir.func{{.*}} private @_ZN1BC1EU7_Atomic1Z(!cir.ptr<!rec_B>{{.*}}, !cir.ptr<!rec_Z>{{.*}}) alias(@_ZN1BC2EU7_Atomic1Z)

// LLVM: @_ZN1BC1EU7_Atomic1Z = unnamed_addr alias void (ptr, ptr), ptr @_ZN1BC2EU7_Atomic1Z
// OGCG: @_ZN1BC1EU7_Atomic1Z = unnamed_addr alias void (ptr, ptr), ptr @_ZN1BC2EU7_Atomic1Z
