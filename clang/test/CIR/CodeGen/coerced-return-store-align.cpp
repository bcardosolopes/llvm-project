// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-call-conv-lowering -emit-llvm -std=c++20 -w %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -std=c++20 -w %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ogcg.ll %s

// Verify that when a coerced return value is stored into an array element at a
// non-zero offset, the store alignment reflects the element's actual address
// (not the base alloca alignment). Without this fix, element[1] at offset 8
// from an align-16 alloca would get align 16 on the store, causing SIGSEGV
// from aligned SSE stores (movdqa) on a misaligned address.

struct S {
  long long x;
};

S getS(int);

// LLVM-LABEL: define {{.*}} @_Z4testv(
// LLVM: alloca [2 x %struct.S], {{.*}}align 16
// LLVM: store i64 %{{.*}}, ptr %{{.*}}, align 8
// LLVM: store i64 %{{.*}}, ptr %{{.*}}, align 8
void test() {
  S arr[2] = { getS(0), getS(1) };
}
