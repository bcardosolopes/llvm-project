// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test that a bare break; before the first case label in a switch doesn't
// crash the CIR verifier. This pattern comes from C/C++ macros that generate
// `switch(x) { break; case X: ... break; case Y: ... }`.

void use(int);

// CHECK: cir.func {{.*}} @_Z25test_break_before_case_v1i
// LLVM: define {{.*}} @_Z25test_break_before_case_v1i
void test_break_before_case_v1(int x) {
  switch (x) {
    break; case 1:
      use(1);
      break;
    break; case 2:
      use(2);
      break;
  }
}
// CHECK: cir.switch
// CHECK: cir.case(equal, [#cir.int<1>
// CHECK: cir.call @_Z3usei
// CHECK: cir.break
// CHECK: cir.case(equal, [#cir.int<2>
// CHECK: cir.call @_Z3usei
// CHECK: cir.break

// LLVM: switch i32 %{{.*}}, label %{{.*}} [
// LLVM:   i32 1, label %{{.*}}
// LLVM:   i32 2, label %{{.*}}
// LLVM: ]
// LLVM: call void @_Z3usei(i32 noundef 1)
// LLVM: call void @_Z3usei(i32 noundef 2)

// Nested switch with break before case.
// CHECK: cir.func {{.*}} @_Z11test_nestedii
// LLVM: define {{.*}} @_Z11test_nestedii
void test_nested(int x, int y) {
  switch (x) {
    case 1:
      switch (y) {
        break; case 10:
          use(10);
          break;
        break; case 20:
          use(20);
          break;
      }
      break;
    break; case 2:
      use(2);
      break;
  }
}
// CHECK: cir.switch
// CHECK: cir.case(equal, [#cir.int<1>
// CHECK: cir.switch
// CHECK: cir.case(equal, [#cir.int<10>
// CHECK: cir.case(equal, [#cir.int<20>

// LLVM: switch i32 %{{.*}}, label %{{.*}} [
// LLVM:   i32 10, label %{{.*}}
// LLVM:   i32 20, label %{{.*}}
// LLVM: ]

// Dead non-terminator code before first case label.
// CHECK: cir.func {{.*}} @_Z23test_dead_code_no_breaki
void test_dead_code_no_break(int x) {
  switch (x) {
    use(99);  // dead code before first case
    case 1:
      use(1);
      break;
  }
}
// CHECK: cir.switch
// CHECK: cir.case(equal, [#cir.int<1>
