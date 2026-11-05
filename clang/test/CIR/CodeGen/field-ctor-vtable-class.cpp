// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test that field constructors in vtable-using classes are called on the
// correct member address (GEP from this), not on a local stack alloca.

struct Base { virtual ~Base(); };
struct Field { Field(); ~Field(); };

struct A : Base {
  Field f;
  A();
};

A::A() {}

// CIR: cir.func {{.*}} @_ZN1AC2Ev
// Compute field address: cast this to i8*, stride by 8, cast to Field*
// CIR:   %[[THIS:.*]] = cir.load %{{.*}} : !cir.ptr<!cir.ptr<!rec_A>>, !cir.ptr<!rec_A>
// CIR:   cir.call @_ZN4BaseC2Ev
// CIR:   %[[I8PTR:.*]] = cir.cast bitcast %[[THIS]] : !cir.ptr<!rec_A> -> !cir.ptr<!u8i>
// CIR:   %[[OFFSET:.*]] = cir.const #cir.int<8> : !s64i
// CIR:   %[[FIELD_I8:.*]] = cir.ptr_stride inbounds %[[I8PTR]], %[[OFFSET]]
// CIR:   %[[FIELD:.*]] = cir.cast bitcast %[[FIELD_I8]] : !cir.ptr<!u8i> -> !cir.ptr<!rec_Field>
// CIR:   cir.call @_ZN5FieldC1Ev(%[[FIELD]]) : (!cir.ptr<!rec_Field>) -> ()

// LLVM: define {{.*}} @_ZN1AC2Ev
// LLVM:   call {{.*}} @_ZN4BaseC2Ev
// LLVM:   %[[GEP:.*]] = getelementptr inbounds i8, ptr %{{.*}}, i64 8
// LLVM:   call {{.*}} @_ZN5FieldC1Ev(ptr {{[^%]*}}%[[GEP]])
