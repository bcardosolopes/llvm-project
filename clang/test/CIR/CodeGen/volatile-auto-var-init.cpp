// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -std=c++17 %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++17 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that volatile auto variable initialization produces volatile
// copy/memcpy operations.

struct S { int x, y; };

void test_volatile_struct_init() {
  volatile S s = {1, 2};
}

// CIR-LABEL: @_Z25test_volatile_struct_initv
// CIR: cir.copy %{{.*}} to %{{.*}} volatile {{.*}}: !cir.ptr<!rec_S>

// LLVM-LABEL: @_Z25test_volatile_struct_initv
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 4 @__const._Z25test_volatile_struct_initv.s, i64 8, i1 true)
