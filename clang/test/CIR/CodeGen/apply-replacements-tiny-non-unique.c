// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Regression test: tiny-set applyReplacements must not rely on
// SymbolTable-based getSymbolUses. Modules that transiently contain non-unique
// symbols (e.g. duplicate pragma-weak aliases before dedup) used to assert.

void __impl(void) {}
#pragma weak weak_alias = __impl
#pragma weak weak_alias = __impl

int np0();
int np1();
int np2();

int call_np0(int x) { return np0(x); }
int call_np1(int x) { return np1(x); }
int call_np2(int x) { return np2(x); }

int np0(int x) { return x + 1; }
int np1(int x) { return x + 2; }
int np2(int x) { return x + 3; }

// CHECK-COUNT-1: cir.alias {{.*}} @weak_alias = @__impl
// CHECK-LABEL: cir.func{{.*}} @call_np0
// CHECK: cir.call @np0(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @call_np1
// CHECK: cir.call @np1(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @call_np2
// CHECK: cir.call @np2(%{{.*}}) {{.*}} -> !s32i

// Keep a same-signature replacement in the TU so tiny-set collection still
// skips it and only fixes mismatched replacements.
int same0(int);
int use_same0(int x) { return same0(x); }
int same0(int x) { return x - 1; }
// CHECK-LABEL: cir.func{{.*}} @use_same0
// CHECK: cir.call @same0

// Ensure direct/indirect uses nested in another function are still discovered
// when mismatch-user scans walk function bodies only.
int np_nested();
int use_nested_direct(int x) { return np_nested(x); }
int use_nested_indirect(int x) {
  int (*fp)(int) = np_nested;
  return fp(x);
}
int np_nested(int x) { return x + 4; }
// CHECK-LABEL: cir.func{{.*}} @use_nested_direct
// CHECK: cir.call @np_nested(%{{.*}}) {{.*}} -> !s32i
// CHECK-LABEL: cir.func{{.*}} @use_nested_indirect
// CHECK: cir.get_global @np_nested
// CHECK: cir.call %{{.*}}(%{{.*}}) {{.*}} -> !s32i
