// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -ftrivial-auto-var-init=zero %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR-ZERO %s --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -ftrivial-auto-var-init=zero %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM-ZERO %s --input-file=%t.ll

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -ftrivial-auto-var-init=pattern %s -o %t-pat.cir
// RUN: FileCheck --check-prefix=CIR-PAT %s --input-file=%t-pat.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -ftrivial-auto-var-init=pattern %s -o %t-pat.ll
// RUN: FileCheck --check-prefix=LLVM-PAT %s --input-file=%t-pat.ll

// Test that VLA trivial initialization emits a memset.

void test_vla_zero_init(int n) {
  int arr[n];
  (void)arr;
}

// CIR-ZERO-LABEL: cir.func{{.*}} @test_vla_zero_init(
// CIR-ZERO: cir.libc.memset{{.*}}set to %{{.*}} {cir.auto_init, cir.dst_align = 16 : i64} : !cir.ptr<!void>, !s32i, !u64i
// LLVM-ZERO-LABEL: define dso_local void @test_vla_zero_init(
// LLVM-ZERO: call void @llvm.memset{{.*}}(ptr align 16 %{{.*}}, i8 0, i64 %{{.*}}, i1 false), !annotation [[AUTO_INIT:![0-9]+]]

void test_vla_pattern_init(int n) {
  int arr[n];
  (void)arr;
}

// Integer VLAs use 0xAA pattern byte.
// CIR-PAT-LABEL: cir.func{{.*}} @test_vla_pattern_init(
// CIR-PAT: cir.libc.memset{{.*}}set to %{{.*}} {cir.auto_init, cir.dst_align = 16 : i64} : !cir.ptr<!void>, !s32i, !u64i
// LLVM-PAT-LABEL: define dso_local void @test_vla_pattern_init(
// LLVM-PAT: call void @llvm.memset{{.*}}(ptr align 16 %{{.*}}, i8 -86, i64 %{{.*}}, i1 false), !annotation [[AUTO_INIT:![0-9]+]]

// Float VLAs use 0xFF pattern byte (NaN).
void test_vla_float_pattern(int n) {
  float arr[n];
  (void)arr;
}

// CIR-PAT-LABEL: cir.func{{.*}} @test_vla_float_pattern(
// CIR-PAT: cir.libc.memset{{.*}}set to %{{.*}} {cir.auto_init, cir.dst_align = 16 : i64} : !cir.ptr<!void>, !s32i, !u64i
// LLVM-PAT-LABEL: define dso_local void @test_vla_float_pattern(
// LLVM-PAT: call void @llvm.memset{{.*}}(ptr align 16 %{{.*}}, i8 -1, i64 %{{.*}}, i1 false), !annotation [[AUTO_INIT]]

// LLVM-ZERO: [[AUTO_INIT]] = !{!"auto-init"}
// LLVM-PAT: [[AUTO_INIT]] = !{!"auto-init"}
