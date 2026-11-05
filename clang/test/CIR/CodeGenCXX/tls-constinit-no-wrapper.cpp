// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --input-file=%t.ll -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll -check-prefix=OGCG

// Test that extern thread_local constinit variables do NOT get _ZTW wrappers
// when the variable has constant initialization and no destructor.

extern thread_local constinit int b;

// Accessing a constinit extern TLS variable uses @llvm.threadlocal.address
// directly — no wrapper call.
// LLVM-LABEL: define {{.*}} @_Z5get_bv()
// LLVM-NOT: call {{.*}}@_ZTW
// LLVM: call {{.*}}ptr @llvm.threadlocal.address.p0(ptr {{.*}} @b)
// LLVM: }
// OGCG-LABEL: define {{.*}} @_Z5get_bv()
// OGCG-NOT: call {{.*}}@_ZTW
// OGCG: call {{.*}}ptr @llvm.threadlocal.address.p0(ptr {{.*}} @b)
// OGCG: }
int get_b() { return b; }

// No wrapper function should be emitted for constinit b.
// LLVM-NOT: define {{.*}} @_ZTW1b()
// OGCG-NOT: define {{.*}} @_ZTW1b()

// PR51079: An incomplete class type might have non-trivial destruction,
// so the wrapper must still be emitted even with constinit.
struct DestructedFwdDecl;
extern thread_local constinit DestructedFwdDecl e3;

// LLVM-LABEL: define {{.*}} @_Z6get_e3v()
// LLVM: call {{.*}}ptr @_ZTW2e3()
// OGCG-LABEL: define {{.*}} @_Z6get_e3v()
// OGCG: call {{.*}}ptr @_ZTW2e3()
DestructedFwdDecl &get_e3() { return e3; }
