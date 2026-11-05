// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

// Compile-time regression: keep an exact 3-symbol replacement map shape live so
// applyReplacements() exercises the dedicated nameMap.size()==3 bulk
// AttrTypeReplacer fast path.

int nr_bulk3_a();
int nr_bulk3_b();
int nr_bulk3_c();

int use_nr_bulk3_a(int x) { return nr_bulk3_a(x); }
int use_nr_bulk3_b(int x) { return nr_bulk3_b(x + 1); }
int use_nr_bulk3_c(int x) { return nr_bulk3_c(x + 2); }

int nr_bulk3_a(int x) { return x + 7; }
int nr_bulk3_b(int x) { return x + 9; }
int nr_bulk3_c(int x) { return x + 11; }

// CHECK-LABEL: cir.func{{.*}} @use_nr_bulk3_a
// CHECK: cir.call @nr_bulk3_a(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_nr_bulk3_b
// CHECK: cir.call @nr_bulk3_b(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_nr_bulk3_c
// CHECK: cir.call @nr_bulk3_c(%{{.*}}) {{.*}} -> !s32i
