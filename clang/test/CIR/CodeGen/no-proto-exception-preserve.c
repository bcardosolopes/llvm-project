// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fexceptions -fclangir -emit-cir %s -o - | FileCheck %s

// Regression: no-proto call rewriting must preserve exceptional call semantics
// and cleanup region contents.

void may_throw();
void cleanup_int(int *);

int use_no_proto_with_cleanup(int x) {
  __attribute__((cleanup(cleanup_int))) int guard = x;
  may_throw();
  return guard;
}

void may_throw(void);
void cleanup_int(int *p) { *p = 0; }

// CHECK-LABEL: cir.func {{.*}} @use_no_proto_with_cleanup
// CHECK: cir.call exception @may_throw() : () -> () cleanup {
// CHECK: cir.call @cleanup_int
