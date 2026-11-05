// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu %s -fclangir -emit-mlir=cir-abi -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu %s -fclangir -emit-llvm -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu %s -emit-llvm -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

typedef unsigned long uword;

__attribute__((interrupt)) void foo7(int *a, uword b) {}

namespace S {
__attribute__((interrupt)) void foo8(int *a) {}
}

// CIR: cir.func {{.*}} @_Z4foo7Pim(%arg0: !cir.ptr<!s32i> {cir.byval = !s32i, cir.noundef}
// CIR-SAME: %arg1: !u64i {cir.noundef}
// CIR-SAME: cc(x86_intrcc)

// CIR: cir.func {{.*}} @_ZN1S4foo8EPi(%arg0: !cir.ptr<!s32i> {cir.byval = !s32i, cir.noundef}
// CIR-SAME: cc(x86_intrcc)

// LLVM: define{{.*}} x86_intrcc void @_Z4foo7Pim(ptr noundef byval(i32) %{{.+}}, i64 noundef %{{.+}})
// LLVM: define{{.*}} x86_intrcc void @_ZN1S4foo8EPi(ptr noundef byval(i32) %{{.+}})

// OGCG: define{{.*}} x86_intrcc void @_Z4foo7Pim(ptr noundef byval(i32) %{{.+}}, i64 noundef %{{.+}})
// OGCG: define{{.*}} x86_intrcc void @_ZN1S4foo8EPi(ptr noundef byval(i32) %{{.+}})
