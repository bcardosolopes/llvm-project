// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// Regression test: function and variable sharing asm label should not crash.
// Previously, getOrCreateCIRFunction asserted when finding a GlobalOp instead
// of a FuncOp for the same symbol name.

// Variable first, then function definition with same asm label.
extern int gvar __asm__("shared_sym");

// CHECK: cir.global "private"  external  @shared_sym.renamed : !s32i
int use_var(void) {
  // CHECK: cir.get_global @shared_sym.renamed : !cir.ptr<!s32i>
  return gvar;
}

// CHECK: cir.func {{.*}} @shared_sym()
void func(void) __asm__("shared_sym");
void func(void) {
}
