// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -Wno-implicit-function-declaration \
// RUN:   -fms-compatibility -fclangir -emit-llvm -o %t.cir.ll %s
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -Wno-implicit-function-declaration \
// RUN:   -fms-compatibility -emit-llvm -o %t.og.ll %s
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// CIR-LABEL: @test_InterlockedIncrement
// CIR: %[[OLD:.*]] = atomicrmw add ptr %{{.*}}, i32 1 seq_cst, align 4
// CIR: %[[NEW:.*]] = add i32 %[[OLD]], 1
// CIR: ret i32
// OGCG-LABEL: @test_InterlockedIncrement
// OGCG: %[[OLD:.*]] = atomicrmw add ptr %{{.*}}, i32 1 seq_cst, align 4
// OGCG: %[[NEW:.*]] = add i32 %[[OLD]], 1
// OGCG: ret i32
int test_InterlockedIncrement(int volatile *p) {
  return _InterlockedIncrement(p);
}

// CIR-LABEL: @test_InterlockedDecrement
// CIR: %[[OLD:.*]] = atomicrmw sub ptr %{{.*}}, i32 1 seq_cst, align 4
// CIR: %[[NEW:.*]] = sub i32 %[[OLD]], 1
// CIR: ret i32
// OGCG-LABEL: @test_InterlockedDecrement
// OGCG: %[[OLD:.*]] = atomicrmw sub ptr %{{.*}}, i32 1 seq_cst, align 4
// OGCG: %[[NEW:.*]] = sub i32 %[[OLD]], 1
// OGCG: ret i32
int test_InterlockedDecrement(int volatile *p) {
  return _InterlockedDecrement(p);
}

// CIR-LABEL: @test_InterlockedIncrement_acq
// CIR: %[[OLD:.*]] = atomicrmw add ptr %{{.*}}, i32 1 acquire, align 4
// CIR: %[[NEW:.*]] = add i32 %[[OLD]], 1
// CIR: ret i32
// OGCG-LABEL: @test_InterlockedIncrement_acq
// OGCG: %[[OLD:.*]] = atomicrmw add ptr %{{.*}}, i32 1 acquire, align 4
// OGCG: %[[NEW:.*]] = add i32 %[[OLD]], 1
// OGCG: ret i32
int test_InterlockedIncrement_acq(int volatile *p) {
  return _InterlockedIncrement_acq(p);
}

// CIR-LABEL: @test_InterlockedDecrement_rel
// CIR: %[[OLD:.*]] = atomicrmw sub ptr %{{.*}}, i32 1 release, align 4
// CIR: %[[NEW:.*]] = sub i32 %[[OLD]], 1
// CIR: ret i32
// OGCG-LABEL: @test_InterlockedDecrement_rel
// OGCG: %[[OLD:.*]] = atomicrmw sub ptr %{{.*}}, i32 1 release, align 4
// OGCG: %[[NEW:.*]] = sub i32 %[[OLD]], 1
// OGCG: ret i32
int test_InterlockedDecrement_rel(int volatile *p) {
  return _InterlockedDecrement_rel(p);
}

// CIR-LABEL: @test_InterlockedIncrement_nf
// CIR: %[[OLD:.*]] = atomicrmw add ptr %{{.*}}, i32 1 monotonic, align 4
// CIR: %[[NEW:.*]] = add i32 %[[OLD]], 1
// CIR: ret i32
// OGCG-LABEL: @test_InterlockedIncrement_nf
// OGCG: %[[OLD:.*]] = atomicrmw add ptr %{{.*}}, i32 1 monotonic, align 4
// OGCG: %[[NEW:.*]] = add i32 %[[OLD]], 1
// OGCG: ret i32
int test_InterlockedIncrement_nf(int volatile *p) {
  return _InterlockedIncrement_nf(p);
}

// CIR-LABEL: @test_InterlockedDecrement_nf
// CIR: %[[OLD:.*]] = atomicrmw sub ptr %{{.*}}, i32 1 monotonic, align 4
// CIR: %[[NEW:.*]] = sub i32 %[[OLD]], 1
// CIR: ret i32
// OGCG-LABEL: @test_InterlockedDecrement_nf
// OGCG: %[[OLD:.*]] = atomicrmw sub ptr %{{.*}}, i32 1 monotonic, align 4
// OGCG: %[[NEW:.*]] = sub i32 %[[OLD]], 1
// OGCG: ret i32
int test_InterlockedDecrement_nf(int volatile *p) {
  return _InterlockedDecrement_nf(p);
}

// CIR-LABEL: @test_InterlockedIncrement64
// CIR: %[[OLD:.*]] = atomicrmw add ptr %{{.*}}, i64 1 seq_cst, align 8
// CIR: %[[NEW:.*]] = add i64 %[[OLD]], 1
// OGCG-LABEL: @test_InterlockedIncrement64
// OGCG: %[[OLD:.*]] = atomicrmw add ptr %{{.*}}, i64 1 seq_cst, align 8
// OGCG: %[[NEW:.*]] = add i64 %[[OLD]], 1
long long test_InterlockedIncrement64(long long volatile *p) {
  return _InterlockedIncrement64(p);
}

// CIR-LABEL: @test_InterlockedDecrement64_rel
// CIR: %[[OLD:.*]] = atomicrmw sub ptr %{{.*}}, i64 1 release, align 8
// CIR: %[[NEW:.*]] = sub i64 %[[OLD]], 1
// OGCG-LABEL: @test_InterlockedDecrement64_rel
// OGCG: %[[OLD:.*]] = atomicrmw sub ptr %{{.*}}, i64 1 release, align 8
// OGCG: %[[NEW:.*]] = sub i64 %[[OLD]], 1
long long test_InterlockedDecrement64_rel(long long volatile *p) {
  return _InterlockedDecrement64_rel(p);
}
