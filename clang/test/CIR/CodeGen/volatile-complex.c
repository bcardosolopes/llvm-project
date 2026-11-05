// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that volatile complex loads/stores are split into element-wise
// operations (one volatile access per element), matching OGCG semantics.

volatile _Complex float cf;
volatile _Complex double cd;
volatile _Complex float cf32 __attribute__((aligned(32)));

// CIR-LABEL: @test_cf
// CIR: cir.load volatile
// CIR: cir.load volatile
// CIR: cir.store volatile
//
// LLVM-LABEL: @test_cf
// LLVM: load volatile float, ptr @cf, align 4
// LLVM: load volatile float, ptr {{.*}}, align 4
// LLVM: load volatile float, ptr @cf, align 4
// LLVM: load volatile float, ptr {{.*}}, align 4
// LLVM: store volatile float {{.*}}, ptr @cf, align 4
// LLVM: store volatile float {{.*}}, ptr {{.*}}, align 4
//
// OGCG-LABEL: @test_cf
// OGCG: load volatile float, ptr @cf, align 4
// OGCG: load volatile float, ptr {{.*}}, align 4
// OGCG: load volatile float, ptr @cf, align 4
// OGCG: load volatile float, ptr {{.*}}, align 4
// OGCG: store volatile float {{.*}}, ptr @cf, align 4
// OGCG: store volatile float {{.*}}, ptr {{.*}}, align 4
void test_cf(void) {
  (void)(cf);
  cf = cf;
}

// CIR-LABEL: @test_cd
// CIR: cir.load volatile
//
// LLVM-LABEL: @test_cd
// LLVM: load volatile double, ptr @cd, align 8
// LLVM: load volatile double, ptr {{.*}}, align 8
//
// OGCG-LABEL: @test_cd
// OGCG: load volatile double, ptr @cd, align 8
// OGCG: load volatile double, ptr {{.*}}, align 8
void test_cd(void) {
  (void)(cd);
  cd = cd;
}

// Check alignment: base align 32, imag part at offset 4 → align 4
// CIR-LABEL: @test_cf32
// CIR: cir.load volatile
//
// LLVM-LABEL: @test_cf32
// LLVM: load volatile float, ptr @cf32, align 32
// LLVM: load volatile float, ptr {{.*}}, align 4
//
// OGCG-LABEL: @test_cf32
// OGCG: load volatile float, ptr @cf32, align 32
// OGCG: load volatile float, ptr {{.*}}, align 4
void test_cf32(void) {
  (void)(cf32);
  cf32 = cf32;
}
