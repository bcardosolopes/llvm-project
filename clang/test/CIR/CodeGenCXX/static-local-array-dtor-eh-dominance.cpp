// RUN: %clang_cc1 -std=c++98 -triple x86_64-unknown-linux-gnu -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s < %t.cir

// Regression test: static local array with non-trivial destructor + a prior
// potentially-throwing call must not produce cross-function SSA references
// in the generated __cxx_global_array_dtor helper.  Previously, the helper's
// EH landing pad captured the enclosing function's local-variable cleanups
// (e.g. the destructor for 'line'), violating MLIR dominance rules.

struct Str {
  Str();
  Str(const char *);
  ~Str();
};

void bar(Str &);

void func() {
  Str line;
  bar(line);
  static const Str a[] = {"x", "y"};
}

// The __cxx_global_array_dtor helper must not reference values from func().
// CHECK-LABEL: cir.func {{.*}} @__cxx_global_array_dtor
// CHECK-NOT: @_Z4funcv
// CHECK: cir.return
