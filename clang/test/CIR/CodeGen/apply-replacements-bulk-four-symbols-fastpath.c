// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Compile-time regression: keep an exact 4-symbol replacement map shape live so
// applyReplacements() exercises the dedicated nameMap.size()==4 bulk
// AttrTypeReplacer fast path.

int nr_bulk4_a();
int nr_bulk4_b();
int nr_bulk4_c();
int nr_bulk4_d();

int use_nr_bulk4_a(int x) { return nr_bulk4_a(x); }
int use_nr_bulk4_b(int x) { return nr_bulk4_b(x + 1); }
int use_nr_bulk4_c(int x) { return nr_bulk4_c(x + 2); }
int use_nr_bulk4_d(int x) { return nr_bulk4_d(x + 3); }

int nr_bulk4_a(int x) { return x + 1; }
int nr_bulk4_b(int x) { return x + 2; }
int nr_bulk4_c(int x) { return x + 3; }
int nr_bulk4_d(int x) { return x + 4; }

// CHECK-LABEL: cir.func{{.*}} @use_nr_bulk4_a
// CHECK: cir.call @nr_bulk4_a(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_nr_bulk4_b
// CHECK: cir.call @nr_bulk4_b(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_nr_bulk4_c
// CHECK: cir.call @nr_bulk4_c(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_nr_bulk4_d
// CHECK: cir.call @nr_bulk4_d(%{{.*}}) {{.*}} -> !s32i
