// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// Regression test: if the fallback rename "sym.renamed" is already used by
// another symbol, function/global asm-label collision handling must pick a
// unique name instead of reusing the existing one.

extern int gvar __asm__("sym");
extern int gvar_collision __asm__("sym.renamed");

int use_collision(void) {
  // CHECK-LABEL: cir.func {{.*}} @use_collision()
  // CHECK: cir.get_global @sym.renamed : !cir.ptr<!s32i>
  return gvar_collision;
}

int use_var(void) {
  // CHECK-LABEL: cir.func {{.*}} @use_var()
  // CHECK: cir.get_global @sym.renamed.1 : !cir.ptr<!s32i>
  return gvar;
}

// CHECK: cir.func {{.*}} @sym()
void func(void) __asm__("sym");
void func(void) {}
