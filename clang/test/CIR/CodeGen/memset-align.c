// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: memset includes alignment on destination pointer.

struct Big { int data[10]; };

void test(void) {
  struct Big b = {{0}};
  (void)b;
}

// CIR: cir.libc.memset {{.*}} {cir.dst_align = 4 : i64}
// LLVM: call void @llvm.memset.p0.i64(ptr align {{[0-9]+}} %{{.*}}, i8 0, i64 40, i1 false)
// OGCG: call void @llvm.memset.p0.i64(ptr align {{[0-9]+}} %{{.*}}, i8 0, i64 40, i1 false)
