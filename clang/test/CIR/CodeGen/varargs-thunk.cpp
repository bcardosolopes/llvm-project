// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s < %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t.ll

// Covariant return type with variadic arguments requires a thunk that
// clones the function body and adjusts this pointer and return value.

struct A { virtual A* f(int x, ...); };
struct B { virtual B* f(int x, ...); };
struct C : A, B { virtual C* f(int x, ...); };
extern C* makeC();
C* C::f(int x, ...) { return makeC(); }

// The B-in-C thunk adjusts this by -8 and return by +8.

// The cloned thunk inherits arg attrs (nonnull, align, dereferenceable) from
// the base function, matching OGCG's llvm::CloneFunction behavior.
// CIR: cir.func {{.*}} @_ZTchn8_h8_N1C1fEiz(%arg0: !cir.ptr<!rec_{{.*}}> {cir.align = 8 : i64, cir.callsite_nonnull, cir.dereferenceable = 16 : i64, cir.nonnull, cir.noundef}
// CIR:   cir.const #cir.int<-8>
// CIR:   cir.ptr_stride inbounds
// CIR:   cir.call @_Z5makeCv()
// CIR:   cir.const #cir.int<8>
// CIR:   cir.ptr_stride inbounds
// CIR:   cir.return

// LLVM: define {{.*}} @_ZTchn8_h8_N1C1fEiz(ptr noundef nonnull align 8 dereferenceable(16)
// LLVM:   getelementptr inbounds i8, ptr %{{.*}}, i64 -8
// LLVM:   call {{.*}} @_Z5makeCv()
// LLVM:   getelementptr inbounds i8, ptr %{{.*}}, i64 8
// LLVM:   ret ptr
