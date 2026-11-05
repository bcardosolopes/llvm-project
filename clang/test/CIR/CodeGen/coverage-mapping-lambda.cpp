// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm -fclangir \
// RUN:   -fprofile-instrument=clang -fcoverage-mapping %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm -fclangir \
// RUN:   -fprofile-instrument=clang -fcoverage-mapping -dump-coverage-mapping %s -o /dev/null | FileCheck %s --check-prefix=DUMP
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm \
// RUN:   -fprofile-instrument=clang -fcoverage-mapping -dump-coverage-mapping %s -o /dev/null | FileCheck %s --check-prefix=DUMP

// A lambda's call operator is emitted on demand (not as a top-level decl), so
// it was instrumented with PGO counters but never recorded for source-based
// coverage mapping, leaving its body with no coverage record. It now gets a
// coverage mapping record like any other emitted function, matching classic
// clang.

int use(int x) {
  auto add = [&](int y) { return x + y; };
  int s = 0;
  for (int i = 0; i < x; ++i)
    s += add(i);
  return s;
}

// Both the enclosing function and the lambda call operator get a coverage
// function record.
// CHECK-DAG: @__covrec_{{[0-9A-F]+}}{{u?}} = linkonce_odr hidden constant
// CHECK-DAG: @__covrec_{{[0-9A-F]+}}{{u?}} = linkonce_odr hidden constant
// CHECK: @__llvm_coverage_mapping = private constant

// The lambda body carries its own mapping region (counter #0) in the same
// shape classic clang emits.
// DUMP-LABEL: _Z3usei:
// DUMP: File 0, {{[0-9]+}}:16
// DUMP-LABEL: {{.*}}clEi:
// DUMP: File 0, {{[0-9]+}}:25 -> {{[0-9]+}}:42 = #0
