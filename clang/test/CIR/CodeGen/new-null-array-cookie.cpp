// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Nothrow array new of non-POD type. When allocation fails (returns
// null), the cookie initialization must NOT be executed, as it would
// dereference the null pointer. The null check must guard ALL post-allocation
// work: cookie init, constructor calls, and operator-delete cleanup.

typedef __typeof__(sizeof(0)) size_t;

namespace std {
  struct nothrow_t {};
}
std::nothrow_t nothrow;

void *operator new[](size_t, const std::nothrow_t &) throw();
void operator delete[](void *, const std::nothrow_t &) throw();

struct S {
  int x;
  S() : x(0) {}
  ~S() {}
};

S* test_nothrow_array_new() {
  return new (nothrow) S[4];
}

// CIR-LABEL: @_Z22test_nothrow_array_newv
// The null check must guard all post-allocation work through a temporary
// result slot.
// CIR: %[[RESULT:.*]] = cir.alloca !cir.ptr<!rec_S>, !cir.ptr<!cir.ptr<!rec_S>>, ["new.result"]
// CIR: cir.if %{{.*}} {
// CIR:   cir.store align(8) %{{.*}}, %[[RESULT]]
// CIR: } else {
// CIR:   cir.store align(8) %{{.*}}, %[[RESULT]]
// CIR: }
// CIR: %{{.*}} = cir.load align(8) %[[RESULT]]

// LLVM-LABEL: @_Z22test_nothrow_array_newv
// The null check branch must happen BEFORE cookie initialization.
// LLVM: call {{.*}} @_ZnamRKSt9nothrow_t(
// LLVM: icmp ne ptr %{{.*}}, null
// LLVM: br i1
// Cookie init and constructors happen only in the not-null path:
// LLVM: store i64 4
// LLVM: call void @_ZN1SC1Ev
// LLVM: store ptr null
// LLVM: load ptr, ptr %{{.*}}

// OGCG-LABEL: @_Z22test_nothrow_array_newv
// OGCG: call {{.*}} @_ZnamRKSt9nothrow_t(
// OGCG: icmp eq ptr %{{.*}}, null
// OGCG: br i1
// OGCG: store i64 4
// OGCG: call void @_ZN1SC1Ev
// OGCG: phi ptr
