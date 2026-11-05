// Verify that the CIR -> LLVM lowering pipeline strips the
// `personality ptr @__gxx_personality_v0` function attribute from functions
// whose `invoke`/`landingpad` instructions are eliminated by LLVM
// optimisations. Functions with surviving EH must keep the attribute.
//
// CIR-to-LLVM lowering attaches `personality` eagerly (see
// CIRToLLVMFuncOpLowering and the landing-pad fallback in
// CIRToLLVMEhTypeIdOpLowering). After downstream LLVM optimisations DCE the
// `invoke`s, the personality reference is left dangling. The
// `CIRStripStalePersonalityPass` registered at OptimizerLast removes it.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fcxx-exceptions -fexceptions -O3 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// CHECK-LABEL: define {{.*}} @keeps_personality_when_eh_survives
// CHECK-SAME:    personality ptr @__gxx_personality_v0
extern "C" int may_throw();
extern "C" int keeps_personality_when_eh_survives() {
  try {
    return may_throw();
  } catch (...) {
    return -1;
  }
}

// `f_dead_eh` has a `try` over a dead branch; LLVM eliminates all invokes,
// so the stale personality attribute should be stripped.
//
// CHECK-LABEL: define {{.*}} @drops_personality_when_eh_dead
// CHECK-NOT:    personality ptr @__gxx_personality_v0
// CHECK-SAME:    {
extern "C" int drops_personality_when_eh_dead(int x) {
  if (false) {
    try {
      throw 42;
    } catch (...) {
      return -1;
    }
  }
  return x + 1;
}
