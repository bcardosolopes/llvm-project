// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -w %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -w %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -w %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that thread_local is preserved on alias definitions.

__thread int TL_WITH_ALIAS;
extern __thread int __libc_errno __attribute__((alias("TL_WITH_ALIAS")));

// CIR: cir.alias external tls_dyn @__libc_errno = @TL_WITH_ALIAS
// LLVM: @__libc_errno = thread_local alias
// OGCG: @__libc_errno = thread_local alias
