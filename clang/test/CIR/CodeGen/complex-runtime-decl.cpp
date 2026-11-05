// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -complex-range=full -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -complex-range=full -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that complex runtime library function DECLARATIONS do NOT have noundef
// on their parameters or return values, matching OGCG which creates these via
// CreateRuntimeFunction (no FunctionDecl, so no noundef).
// The CALL SITES should still have noundef on parameters and return.

_Complex float cf_mul(_Complex float a, _Complex float b) {
  return a * b;
}

_Complex double cd_div(_Complex double a, _Complex double b) {
  return a / b;
}

// Declarations should NOT have noundef.
// LLVM-DAG: declare <2 x float> @__mulsc3(float, float, float, float)
// OGCG-DAG: declare <2 x float> @__mulsc3(float, float, float, float)
// LLVM-DAG: declare { double, double } @__divdc3(double, double, double, double)
// OGCG-DAG: declare { double, double } @__divdc3(double, double, double, double)

// Call sites SHOULD have noundef on parameters and return (in C++ mode).
// LLVM-DAG: call noundef <2 x float> @__mulsc3(float noundef %{{.*}}, float noundef %{{.*}}, float noundef %{{.*}}, float noundef %{{.*}})
// OGCG-DAG: call noundef <2 x float> @__mulsc3(float noundef %{{.*}}, float noundef %{{.*}}, float noundef %{{.*}}, float noundef %{{.*}})
// LLVM-DAG: call noundef { double, double } @__divdc3(double noundef %{{.*}}, double noundef %{{.*}}, double noundef %{{.*}}, double noundef %{{.*}})
// OGCG-DAG: call noundef { double, double } @__divdc3(double noundef %{{.*}}, double noundef %{{.*}}, double noundef %{{.*}}, double noundef %{{.*}})
