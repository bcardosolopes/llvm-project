// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Regression test: mem2reg must not promote/strip a volatile cir.copy. A
// volatile aggregate copy is an externally observable access (e.g. MMIO) and
// must survive mem2reg with its volatile qualifier intact, matching the
// LoadOp/StoreOp handling and classic codegen (volatile memcpy).

struct S {
  int a, b;
};

void f(volatile struct S *mmio) {
  volatile struct S local;
  local = *mmio;
  *mmio = local;
}

// The volatile copies must survive mem2reg (not be promoted to plain
// non-volatile load/store).
// CHECK-LABEL: cir.func {{.*}}@f(
// CHECK:         cir.copy {{.*}} volatile
// CHECK:         cir.copy {{.*}} volatile
// CHECK:       }

// At the LLVM IR level, both observable accesses must remain volatile
// memcpy (i1 true), not plain non-volatile ops.
// LLVM-LABEL: define {{.*}}void @f(
// LLVM:         call void @llvm.memcpy.{{.*}}, i1 true)
// LLVM:         call void @llvm.memcpy.{{.*}}, i1 true)
// LLVM:       }
