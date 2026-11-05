// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that llvm.threadlocal.address intrinsic calls carry alignment
// attributes on both the parameter and return value, matching OGCG.

thread_local int tls_int = 42;
thread_local double tls_double = 3.14;
thread_local char tls_char = 'x';

struct S { long a; long b; long c; };
thread_local S tls_struct = {1, 2, 3};

int read_int() { return tls_int; }
// LLVM-LABEL: @_Z8read_intv
// LLVM: call align 4 ptr @llvm.threadlocal.address.p0(ptr align 4 @tls_int)
// OGCG-LABEL: @_Z8read_intv
// OGCG: call align 4 ptr @llvm.threadlocal.address.p0(ptr align 4 @tls_int)

double read_double() { return tls_double; }
// LLVM-LABEL: @_Z11read_doublev
// LLVM: call align 8 ptr @llvm.threadlocal.address.p0(ptr align 8 @tls_double)
// OGCG-LABEL: @_Z11read_doublev
// OGCG: call align 8 ptr @llvm.threadlocal.address.p0(ptr align 8 @tls_double)

char read_char() { return tls_char; }
// LLVM-LABEL: @_Z9read_charv
// LLVM: call align 1 ptr @llvm.threadlocal.address.p0(ptr align 1 @tls_char)
// OGCG-LABEL: @_Z9read_charv
// OGCG: call align 1 ptr @llvm.threadlocal.address.p0(ptr align 1 @tls_char)

long read_struct() { return tls_struct.a; }
// LLVM-LABEL: @_Z11read_structv
// LLVM: call align 8 ptr @llvm.threadlocal.address.p0(ptr align 8 @tls_struct)
// OGCG-LABEL: @_Z11read_structv
// OGCG: call align 8 ptr @llvm.threadlocal.address.p0(ptr align 8 @tls_struct)
