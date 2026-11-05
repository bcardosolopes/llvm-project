// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Verify that __imag__ / __real__ on a volatile complex lvalue loads only the
// needed element, not the entire complex value. Previously CIR loaded both
// components even when only one was needed, causing spurious volatile side
// effects.

volatile _Complex int ci;

void test_imag_only() {
  __imag ci = __imag ci = __imag ci;
}

// Only imaginary-part accesses — no load of the real part.
// LLVM-LABEL: @_Z14test_imag_onlyv
// LLVM-NOT: load volatile i32, ptr @ci,
// LLVM: load volatile i32, ptr getelementptr inbounds nuw
// LLVM: store volatile i32 {{.*}}, ptr getelementptr inbounds nuw
// LLVM: load volatile i32, ptr getelementptr inbounds nuw
// LLVM: store volatile i32 {{.*}}, ptr getelementptr inbounds nuw
// LLVM-NOT: load volatile i32, ptr @ci,
//
// OGCG-LABEL: @_Z14test_imag_onlyv
// OGCG-NOT: load volatile i32, ptr @ci,
// OGCG: load volatile i32, ptr getelementptr inbounds nuw
// OGCG: store volatile i32 {{.*}}, ptr getelementptr inbounds nuw
// OGCG: load volatile i32, ptr getelementptr inbounds nuw
// OGCG: store volatile i32 {{.*}}, ptr getelementptr inbounds nuw
// OGCG-NOT: load volatile i32, ptr @ci,

void test_real_only() {
  __real ci = __real ci;
}

// Only real-part accesses — no load of the imaginary part.
// LLVM-LABEL: @_Z14test_real_onlyv
// LLVM: load volatile i32, ptr @ci
// LLVM: store volatile i32 {{.*}}, ptr @ci
// LLVM-NOT: load volatile i32, ptr getelementptr
//
// OGCG-LABEL: @_Z14test_real_onlyv
// OGCG: load volatile i32, ptr @ci
// OGCG: store volatile i32 {{.*}}, ptr @ci
// OGCG-NOT: load volatile i32, ptr getelementptr
