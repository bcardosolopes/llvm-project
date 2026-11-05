// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffp-exception-behavior=strict -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -ffp-exception-behavior=strict -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Verify that strictfp attribute is propagated to both function definitions
// and call sites when strict FP semantics are enabled.

double add(double a, double b) {
  return a + b;
}

// CIR-DAG: #[[STRICT_ATTR:.*]] = #cir<extra({strictfp})>
// CIR: cir.call @add({{.*}}) {{.*}} extra(#[[STRICT_ATTR]])
void caller() {
  add(1.0, 2.0);
}

// Function definitions have strictfp.
// LLVM: define {{.*}} @add({{.*}}) #[[FUNC_ATTR:[0-9]+]]
// LLVM: attributes #[[FUNC_ATTR]] = { {{.*}}strictfp{{.*}} }
