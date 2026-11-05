// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

struct Guard {
  Guard();
  ~Guard();
};

void use();

// Verify that temporaries in if/else branches are properly scoped —
// destructors must be called at the end of each branch, not leaked.
void test_if_cleanup(bool cond) {
  if (cond) {
    Guard g;
    use();
  } else {
    Guard g;
    use();
  }
}

// CHECK: cir.func {{.*}} @_Z15test_if_cleanupb
// CHECK:   cir.if
// CHECK:     cir.call @_ZN5GuardC1Ev
// CHECK:     cir.call @_Z3usev
// CHECK:     cir.call @_ZN5GuardD1Ev
// CHECK:   } else {
// CHECK:     cir.call @_ZN5GuardC1Ev
// CHECK:     cir.call @_Z3usev
// CHECK:     cir.call @_ZN5GuardD1Ev
// CHECK:   }
