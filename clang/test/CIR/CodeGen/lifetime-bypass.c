// RUN: %clang_cc1 -emit-llvm -o - -O2 -disable-llvm-passes -fclangir %s \
// RUN:   | FileCheck %s
//
// Verify that CIR suppresses lifetime markers for variables whose scope is
// bypassed by goto or switch statements, matching OGCG behavior.

extern int use(char *p, int n);

// CHECK: @__const.indirect_goto_bypass.T = private unnamed_addr constant [1 x ptr] [ptr blockaddress(@indirect_goto_bypass, %{{.*}})], align 8

// Goto bypass: 'x' is declared between the label and the goto.
// The goto jumps backward past 'x', so lifetime markers must be suppressed.
//
// CHECK-LABEL: @goto_bypass
// CHECK-NOT: llvm.lifetime
// CHECK: }
void goto_bypass(void) {
  {
    char x;
l1:
    use(&x, 1);
  }
  goto l1;
}

// Switch bypass: 'x' is declared in case 1 but accessible from case 2.
// The switch jumps past 'x', so lifetime markers must be suppressed.
//
// CHECK-LABEL: @switch_bypass
// CHECK-NOT: llvm.lifetime
// CHECK: }
void switch_bypass(int n) {
  switch (n) {
  case 1:
    n = n;
    char x;
    use(&x, 1);
    break;
  case 2:
    use(&x, 1);
    break;
  }
}

// Indirect goto: the compiler can't determine all jump targets,
// so all lifetime markers in the function are suppressed.
//
// CHECK-LABEL: @indirect_goto_bypass
// CHECK-NOT: llvm.lifetime
// CHECK: }
void indirect_goto_bypass(int n) {
  char x;
  void *T[] = {&&L};
  goto *T[n];
L:
  use(&x, 1);
}

// C-specific backward label suppression: in C, a label before a variable
// declaration means the variable's lifetime could be re-entered by a backward
// jump, so lifetime markers are suppressed for the variable after the label.
//
// CHECK-LABEL: @label_before_decl
// CHECK-NOT: llvm.lifetime
// CHECK: }
void label_before_decl(void) {
l1:
  use((char*)0, 0);
  char y[5];
  use(y, 5);
  goto l1;
}
