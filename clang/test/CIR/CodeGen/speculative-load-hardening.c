// RUN: %clang_cc1 -triple x86_64-unknown-unknown -mspeculative-load-hardening -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM %s < %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -mspeculative-load-hardening -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s < %t.ogcg.ll

// Verify that -mspeculative-load-hardening emits speculative_load_hardening as
// a proper LLVM enum attribute (with underscores, unquoted), not a string
// attribute "speculative-load-hardening" (with hyphens, quoted).

int test1(void) {
  return 42;
}
// LLVM: @{{.*}}test1{{.*}}[[SLH:#[0-9]+]]
// OGCG: @{{.*}}test1{{.*}}[[SLH:#[0-9]+]]

// LLVM: attributes [[SLH]] = { {{.*}}speculative_load_hardening{{.*}} }
// OGCG: attributes [[SLH]] = { {{.*}}speculative_load_hardening{{.*}} }
