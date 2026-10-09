// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=BOTH
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=BOTH

__attribute__((optnone)) void optnone_fn(void) {}
// CIR: cir.func no_inline{{.*}}@optnone_fn()
// CIR-SAME: optnone
// BOTH: define{{.*}}@optnone_fn(){{.*}} #[[OPTNONE_ATTR:.*]] {

void plain_fn(void) {}
// CIR: cir.func{{.*}}@plain_fn()
// CIR-NOT: optnone,
// CIR-NOT: optnone}
// BOTH: define{{.*}}@plain_fn(){{.*}} #[[PLAIN_ATTR:.*]] {
// BOTH-NOT: optnone

// BOTH: attributes #[[OPTNONE_ATTR]]
// BOTH-DAG: noinline
// BOTH-DAG: optnone
//
// BOTH: attributes #[[PLAIN_ATTR]]
// BOTH-NOT: optnone
//
// Bounds the NOT regions and the DAG above. Load-bearing: the clang version
// string below carries the checkout path, which may itself say "optnone".
// BOTH: !llvm.
