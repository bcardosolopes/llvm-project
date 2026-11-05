// RUN: %clang_cc1 -triple armv7-none-linux-androideabi -mfloat-abi hard -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple armv7-none-linux-androideabi -mfloat-abi hard -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Oversized atomics lower to the __atomic_* runtime libcall. On 32-bit ARM
// hard-float the runtime calling convention is arm_aapcs_vfpcc; the emitted
// call must carry it (matching the declaration createRuntimeFunction stamps),
// otherwise the CIR verifier rejects the call-vs-callee CC mismatch.

unsigned char a1[100], a2[100];

// CIR:       declare arm_aapcs_vfpcc void @__atomic_load
// CIR-LABEL: define{{.*}} void @test_big_atomic
// CIR:         call arm_aapcs_vfpcc void @__atomic_load
//
// OGCG-LABEL: define{{.*}} void @test_big_atomic
// OGCG:        call arm_aapcs_vfpcc void @__atomic_load
void test_big_atomic(void) {
  (void)__atomic_load(&a1, &a2, 5);
}
