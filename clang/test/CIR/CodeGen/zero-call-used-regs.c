// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Per-function zero_call_used_regs attribute.
void f_skip(void) __attribute__((zero_call_used_regs("skip")));
void f_skip(void) {}

void f_all(void) __attribute__((zero_call_used_regs("all")));
void f_all(void) {}

void f_used_gpr(void) __attribute__((zero_call_used_regs("used-gpr")));
void f_used_gpr(void) {}

void f_plain(void) {}

// LLVM: define dso_local void @f_skip()
// LLVM: define dso_local void @f_all()
// LLVM: define dso_local void @f_used_gpr()
// LLVM: define dso_local void @f_plain()
// LLVM-DAG: "zero-call-used-regs"="skip"
// LLVM-DAG: "zero-call-used-regs"="all"
// LLVM-DAG: "zero-call-used-regs"="used-gpr"

// OGCG: define dso_local void @f_skip()
// OGCG: define dso_local void @f_all()
// OGCG: define dso_local void @f_used_gpr()
// OGCG: define dso_local void @f_plain()
// OGCG-DAG: "zero-call-used-regs"="skip"
// OGCG-DAG: "zero-call-used-regs"="all"
// OGCG-DAG: "zero-call-used-regs"="used-gpr"
