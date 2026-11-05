// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fms-extensions -fclangir \
// RUN:   -emit-llvm -o %t.cir.ll %s
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fms-extensions \
// RUN:   -emit-llvm -o %t.og.ll %s
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

// CIR-LABEL: @test_InterlockedIncrement16
// CIR: %[[OLD:.*]] = atomicrmw add ptr %{{.*}}, i16 1 seq_cst, align 2
// CIR: %[[NEW:.*]] = add i16 %[[OLD]], 1
// CIR: ret i16
// OGCG-LABEL: @test_InterlockedIncrement16
// OGCG: %[[OLD:.*]] = atomicrmw add ptr %{{.*}}, i16 1 seq_cst, align 2
// OGCG: %[[NEW:.*]] = add i16 %[[OLD]], 1
// OGCG: ret i16
short test_InterlockedIncrement16(short volatile *p) {
  return _InterlockedIncrement16(p);
}

// CIR-LABEL: @test_InterlockedDecrement16
// CIR: %[[OLD:.*]] = atomicrmw sub ptr %{{.*}}, i16 1 seq_cst, align 2
// CIR: %[[NEW:.*]] = sub i16 %[[OLD]], 1
// CIR: ret i16
// OGCG-LABEL: @test_InterlockedDecrement16
// OGCG: %[[OLD:.*]] = atomicrmw sub ptr %{{.*}}, i16 1 seq_cst, align 2
// OGCG: %[[NEW:.*]] = sub i16 %[[OLD]], 1
// OGCG: ret i16
short test_InterlockedDecrement16(short volatile *p) {
  return _InterlockedDecrement16(p);
}
