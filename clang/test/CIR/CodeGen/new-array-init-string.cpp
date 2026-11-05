// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// Test C++20 parenthesized string literal init in new-array expressions:
//   new char[n]("abc")
// The string is memcpy'd into the beginning of the array and the rest
// is memset to zero.

void test_dynamic(int n) {
  new char[n]("abc");
}

// CIR-LABEL: @_Z12test_dynamici
// CIR: cir.call @_Znam
// CIR: cir.copy
// CIR: cir.libc.memset

// LLVM-LABEL: @_Z12test_dynamici
// LLVM: call {{.*}}ptr @_Znam
// LLVM: call void @llvm.memcpy.p0.p0.i64({{.*}}, i64 4,
// LLVM: getelementptr inbounds i8, ptr %{{.*}}, i64 4
// LLVM: call void @llvm.memset.p0.i64({{.*}}, i8 0,

// OGCG-LABEL: @_Z12test_dynamici
// OGCG: call {{.*}}ptr @_Znam
// OGCG: call void @llvm.memcpy.p0.p0.i64({{.*}}, i64 4,
// OGCG: getelementptr inbounds i8, ptr %{{.*}}, i64 4
// OGCG: call void @llvm.memset.p0.i64({{.*}}, i8 0,

void test_parens(int n) {
  new char[n](("abc"));
}

// CIR-LABEL: @_Z11test_parensi
// CIR: cir.call @_Znam
// CIR: cir.copy
// CIR: cir.libc.memset

// LLVM-LABEL: @_Z11test_parensi
// LLVM: call {{.*}}ptr @_Znam
// LLVM: call void @llvm.memcpy.p0.p0.i64({{.*}}, i64 4,

// OGCG-LABEL: @_Z11test_parensi
// OGCG: call {{.*}}ptr @_Znam
// OGCG: call void @llvm.memcpy.p0.p0.i64({{.*}}, i64 4,
