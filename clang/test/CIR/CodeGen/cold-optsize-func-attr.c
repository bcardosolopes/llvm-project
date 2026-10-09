// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=BOTH
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -disable-llvm-passes -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=BOTH
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O0 -disable-llvm-passes -DO0_BUILD -fclangir -emit-cir %s -o %t0.cir
// RUN: FileCheck --input-file=%t0.cir %s -check-prefix=CIR0
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O0 -disable-llvm-passes -DO0_BUILD -fclangir -emit-llvm %s -o %t0.ll
// RUN: FileCheck --input-file=%t0.ll %s -check-prefix=BOTH0
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O0 -disable-llvm-passes -DO0_BUILD -emit-llvm %s -o %t0.ll
// RUN: FileCheck --input-file=%t0.ll %s -check-prefix=BOTH0

__attribute__((cold)) void cold_decl(void);

__attribute__((cold)) void cold_fn(void) {}
// CIR: cir.func{{.*}}@cold_fn()
// CIR-SAME: cold
// CIR-SAME: optsize
// BOTH: define{{.*}}@cold_fn(){{.*}} #[[COLD_ATTR:[0-9]+]] {
// CIR0: cir.func{{.*}}@cold_fn()
// CIR0-NOT: optsize
// BOTH0: define{{.*}}@cold_fn(){{.*}} #[[COLD0_ATTR:[0-9]+]] {

void plain_fn(void) {}
// CIR: cir.func{{.*}}@plain_fn()
// CIR-NOT: optsize
// BOTH: define{{.*}}@plain_fn(){{.*}} #[[PLAIN_ATTR:[0-9]+]] {

// optnone suppresses optsize. No shared group check: without optnone
// modeling the LLVM groups differ (classic adds noinline/optnone there).
__attribute__((cold)) __attribute__((optnone)) void cold_optnone_fn(void) {}
// CIR: cir.func{{.*}}@cold_optnone_fn()
// CIR-NOT: optsize
// BOTH: define{{.*}}@cold_optnone_fn(){{.*}} {

// minsize itself is unmodeled; only its optsize interaction is pinned, and only
// at O0, where it is the only thing that keeps optsize on a cold function.
#ifdef O0_BUILD
__attribute__((cold)) __attribute__((minsize)) void cold_minsize_fn(void) {}
#endif
// CIR0: cir.func{{.*}}@cold_minsize_fn()
// CIR0-SAME: cold
// CIR0-SAME: optsize
// BOTH0: define{{.*}}@cold_minsize_fn(){{.*}} #[[MINSIZE0_ATTR:[0-9]+]] {

void caller_fn(void) {
  cold_fn();
  cold_decl();
}
// CIR: cir.func{{.*}}@caller_fn()
// CIR: cir.call{{.*}}@cold_fn()
// CIR-NOT: optsize
// CIR: cir.call{{.*}}@cold_decl()
// CIR-NOT: optsize
// CIR: cir.return
// BOTH: define{{.*}}@caller_fn()
// BOTH: call void @cold_fn() #[[CALL_ATTR:[0-9]+]]
// BOTH: call void @cold_decl()

// Declarations emit last in both pipelines.
// CIR: cir.func{{.*}}@cold_decl()
// Delimited: the trailing #loc lines carry this file's own name.
// CIR-NOT: optsize,
// CIR-NOT: optsize}
// BOTH: declare{{.*}}@cold_decl(){{.*}} #[[DECL_ATTR:[0-9]+]]

// BOTH: attributes #[[COLD_ATTR]] = {
// BOTH-SAME: cold
// BOTH-SAME: optsize
// BOTH: attributes #[[PLAIN_ATTR]] = {
// BOTH-NOT: optsize
// BOTH: attributes #[[DECL_ATTR]] = {
// BOTH-NOT: optsize
// BOTH: attributes #[[CALL_ATTR]] = { cold }

// BOTH0: attributes #[[COLD0_ATTR]] = {
// BOTH0-NOT: optsize
// BOTH0: attributes #[[MINSIZE0_ATTR]] = {
// BOTH0-SAME: optsize
