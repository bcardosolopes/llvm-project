// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that uniform-byte arrays use memset instead of creating large
// constant globals. Matches OGCG's shouldUseMemSetToInitialize
// (CGDecl.cpp:1017-1024).

void test_uniform(void) {
  // All 1000 bytes are 'A' (0x41) — should use memset, not a 1KB global.
  char buf[1000] = { [0 ... 999] = 'A' };
}

// CIR-LABEL: cir.func{{.*}} @test_uniform()
// CIR: cir.libc.memset %{{.*}} bytes from %{{.*}} set to %{{.*}} {cir.dst_align = 16 : i64} : !cir.ptr<!void>, !s32i, !u64i
// CIR-NOT: cir.get_global @__const.test_uniform
// CIR-NOT: cir.copy

// LLVM-LABEL: define dso_local void @test_uniform()
// LLVM: call void @llvm.memset{{.*}}(ptr align 16 %{{.*}}, i8 65, i64 1000
// LLVM-NOT: call void @llvm.memcpy

void test_uniform_float(void) {
  float arr[9] = {0x1.cacacap+75, 0x1.cacacap+75, 0x1.cacacap+75,
                  0x1.cacacap+75, 0x1.cacacap+75, 0x1.cacacap+75,
                  0x1.cacacap+75, 0x1.cacacap+75, 0x1.cacacap+75};
}

// CIR-LABEL: cir.func{{.*}} @test_uniform_float()
// CIR: cir.libc.memset %{{.*}} bytes from %{{.*}} set to %{{.*}} {cir.dst_align = 16 : i64} : !cir.ptr<!void>, !s32i, !u64i
// CIR-NOT: cir.get_global @__const.test_uniform_float
// CIR-NOT: cir.copy

// LLVM-LABEL: define dso_local void @test_uniform_float()
// LLVM: call void @llvm.memset{{.*}}(ptr align 16 %{{.*}}, i8 101, i64 36
// LLVM-NOT: call void @llvm.memcpy

void test_uniform_double(void) {
  double arr[7] = {0x1.4444444444444p+69, 0x1.4444444444444p+69,
                   0x1.4444444444444p+69, 0x1.4444444444444p+69,
                   0x1.4444444444444p+69, 0x1.4444444444444p+69,
                   0x1.4444444444444p+69};
}

// CIR-LABEL: cir.func{{.*}} @test_uniform_double()
// CIR: cir.libc.memset %{{.*}} bytes from %{{.*}} set to %{{.*}} {cir.dst_align = 16 : i64} : !cir.ptr<!void>, !s32i, !u64i
// CIR-NOT: cir.get_global @__const.test_uniform_double
// CIR-NOT: cir.copy

// LLVM-LABEL: define dso_local void @test_uniform_double()
// LLVM: call void @llvm.memset{{.*}}(ptr align 16 %{{.*}}, i8 68, i64 56
// LLVM-NOT: call void @llvm.memcpy
