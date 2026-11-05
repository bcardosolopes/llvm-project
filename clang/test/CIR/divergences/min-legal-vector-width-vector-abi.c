// RUN: %clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O0 -fno-clangir %s -o %t.og.ll
// RUN: %clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O0 -fclangir %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=COMMON --input-file=%t.og.ll %s
// RUN: FileCheck --check-prefix=COMMON --input-file=%t.cir.ll %s
//
// Regression: vector ABI coercion paths should not perturb
// "min-legal-vector-width". We intentionally check only the target attribute
// here; function-body shape is tracked by separate divergence tests.

typedef char c8 __attribute__((vector_size(8)));
typedef long long ll1 __attribute__((vector_size(8)));

void f_c8(c8 v) {}
void f_ll1(ll1 v) {}

c8 r_c8() { return (c8){0}; }
ll1 r_ll1() { return (ll1){0}; }

// COMMON-LABEL: define{{.*}} @f_c8(
// COMMON-LABEL: define{{.*}} @f_ll1(
// COMMON-LABEL: define{{.*}} @r_c8(
// COMMON-LABEL: define{{.*}} @r_ll1(
// COMMON: attributes #0 = { {{.*}} "min-legal-vector-width"="0" {{.*}} }
