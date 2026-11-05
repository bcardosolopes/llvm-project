// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

//===----------------------------------------------------------------------===//
// Data member pointer: access via ->*
//===----------------------------------------------------------------------===//

struct S {
    int x, y;
};

int access_data_member(S* s, int S::*ptr) {
    return s->*ptr;
}

// Data member pointer is a single i64 offset.
// CIR: cir.func {{.*}} @_Z18access_data_memberP1SMS_i(%arg0: !cir.ptr<!rec_S> {{.*}}, %arg1: !s64i {{.*}}) -> {{.*}}!s32i
// CIR:   %[[S:.*]] = cir.load {{.*}} : !cir.ptr<!cir.ptr<!rec_S>>, !cir.ptr<!rec_S>
// CIR:   %[[OFF:.*]] = cir.load {{.*}} : !cir.ptr<!s64i>, !s64i
// CIR:   %[[CAST:.*]] = cir.cast bitcast %[[S]] : !cir.ptr<!rec_S> -> !cir.ptr<!s8i>
// CIR:   %[[GEP:.*]] = cir.ptr_stride inbounds %[[CAST]], %[[OFF]] : (!cir.ptr<!s8i>, !s64i) -> !cir.ptr<!s8i>
// CIR:   %[[RESULT_PTR:.*]] = cir.cast bitcast %[[GEP]] : !cir.ptr<!s8i> -> !cir.ptr<!s32i>
// CIR:   %[[RESULT:.*]] = cir.load {{.*}} %[[RESULT_PTR]] : !cir.ptr<!s32i>, !s32i

// LLVM: define {{.*}} noundef i32 @_Z18access_data_memberP1SMS_i(ptr noundef %{{.*}}, i64 %{{.*}})
// LLVM:   %[[S:.*]] = load ptr, ptr %{{.*}}, align 8
// LLVM:   %[[OFF:.*]] = load i64, ptr %{{.*}}, align 8
// LLVM:   %[[GEP:.*]] = getelementptr inbounds i8, ptr %[[S]], i64 %[[OFF]]
// LLVM:   %[[VAL:.*]] = load i32, ptr %[[GEP]], align 4

// OGCG: define {{.*}} i32 @_Z18access_data_memberP1SMS_i(ptr {{.*}} %s, i64 %ptr)
// OGCG:   %[[S:.*]] = load ptr, ptr %s.addr, align 8
// OGCG:   %[[OFF:.*]] = load i64, ptr %ptr.addr, align 8
// OGCG:   %[[GEP:.*]] = getelementptr inbounds i8, ptr %[[S]], i64 %[[OFF]]
// OGCG:   %[[VAL:.*]] = load i32, ptr %[[GEP]], align 4

//===----------------------------------------------------------------------===//
// Null member pointer comparison
//===----------------------------------------------------------------------===//

bool is_null(int S::*ptr) {
    return ptr == nullptr;
}

// Null data member pointer is represented as -1.
// CIR: cir.func {{.*}} @_Z7is_nullM1Si(%arg0: !s64i {{.*}}) -> ({{.*}}!cir.bool
// CIR:   %[[PTR:.*]] = cir.load {{.*}} : !cir.ptr<!s64i>, !s64i
// CIR:   %[[NULL:.*]] = cir.const #cir.int<-1> : !s64i
// CIR:   %[[CMP:.*]] = cir.cmp(eq, %[[PTR]], %[[NULL]]) : !s64i, !cir.bool

// LLVM: define {{.*}} noundef zeroext i1 @_Z7is_nullM1Si(i64 %{{.*}})
// LLVM:   icmp eq i64 %{{.*}}, -1

// OGCG: define {{.*}} i1 @_Z7is_nullM1Si(i64 %ptr)
// OGCG:   icmp eq i64 %{{.*}}, -1

//===----------------------------------------------------------------------===//
// Member function pointer: ABI decomposition and dispatch
//===----------------------------------------------------------------------===//

struct Obj {
    int x;
    int foo(int y) { return x + y; }
};

int call_member_fn(Obj* o, int (Obj::*ptr)(int), int val) {
    return (o->*ptr)(val);
}

// Member function pointer {i64, i64} is decomposed into two i64 params per
// x86_64 ABI. Both CIR and OGCG must match this decomposition.
//
// CIR: cir.func {{.*}} @_Z14call_member_fnP3ObjMS_FiiEi(%arg0: !cir.ptr<!rec_Obj> {{.*}}, %arg1: !s64i {{.*}}, %arg2: !s64i {{.*}}, %arg3: !s32i {{.*}}) -> {{.*}}!s32i
//
// LLVM: define {{.*}} noundef i32 @_Z14call_member_fnP3ObjMS_FiiEi(ptr noundef %{{.*}}, i64 %{{.*}}, i64 %{{.*}}, i32 noundef %{{.*}})
// LLVM:   extractvalue { i64, i64 } %{{.*}}, 1
// LLVM:   extractvalue { i64, i64 } %{{.*}}, 0
// LLVM:   and i64 %{{.*}}, 1
// LLVM:   icmp eq i64 %{{.*}}, 1
// LLVM:   br i1 %{{.*}}, label %{{.*}}, label %{{.*}}
// LLVM:   load ptr, ptr %{{.*}}, align 8
// LLVM:   inttoptr i64 %{{.*}} to ptr
// LLVM:   phi ptr
// LLVM:   call noundef i32 %{{.*}}(ptr noundef nonnull align 4 dereferenceable(4) %{{.*}}, i32 noundef %{{.*}})
//
// OGCG: define {{.*}} i32 @_Z14call_member_fnP3ObjMS_FiiEi(ptr {{.*}} %o, i64 %ptr.coerce0, i64 %ptr.coerce1, i32 {{.*}} %val)
// OGCG:   %memptr.adj = extractvalue { i64, i64 } %{{.*}}, 1
// OGCG:   %memptr.ptr = extractvalue { i64, i64 } %{{.*}}, 0
// OGCG:   and i64 %memptr.ptr, 1
// OGCG:   %memptr.isvirtual = icmp ne i64 %{{.*}}, 0
// OGCG:   br i1 %memptr.isvirtual, label %memptr.virtual, label %memptr.nonvirtual

//===----------------------------------------------------------------------===//
// Base-to-derived: data member pointer on derived class
//===----------------------------------------------------------------------===//

struct Base {
    int x;
};

struct Derived : Base {
    int y;
};

int access_base_member(Derived* d, int Base::*ptr) {
    return d->*ptr;
}

// Single inheritance: no offset adjustment needed. Same as plain data member.
// LLVM: define {{.*}} noundef i32 @_Z18access_base_memberP7DerivedM4Basei(ptr noundef %{{.*}}, i64 %{{.*}})
// LLVM:   getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}

// OGCG: define {{.*}} i32 @_Z18access_base_memberP7DerivedM4Basei(ptr {{.*}} %d, i64 %ptr)
// OGCG:   getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}

//===----------------------------------------------------------------------===//
// Multiple inheritance: non-first base requires this-adjustment
//===----------------------------------------------------------------------===//

struct A { int a; };
struct B { int b; };
struct C : A, B { int c; };

int access_second_base(C* c, int B::*ptr) {
    return c->*ptr;
}

// Multiple inheritance requires null check and base adjustment.
// LLVM: define {{.*}} noundef i32 @_Z18access_second_baseP1CM1Bi(ptr noundef %{{.*}}, i64 %{{.*}})
// LLVM:   icmp eq ptr %{{.*}}, null
// LLVM:   getelementptr inbounds i8, ptr %{{.*}}, i64 4
// LLVM:   select i1 %{{.*}}, ptr %{{.*}}, ptr %{{.*}}

// OGCG: define {{.*}} i32 @_Z18access_second_baseP1CM1Bi(ptr {{.*}} %c, i64 %ptr)
// OGCG:   icmp eq ptr %{{.*}}, null
// OGCG:   getelementptr inbounds i8, ptr %{{.*}}, i64 4

//===----------------------------------------------------------------------===//
// Virtual member function pointer dispatch
//===----------------------------------------------------------------------===//

struct VBase {
    virtual int foo() { return 1; }
};

int call_virtual(VBase* b, int (VBase::*ptr)()) {
    return (b->*ptr)();
}

// Virtual member function pointer: same ABI decomposition as non-virtual.
// LLVM: define {{.*}} noundef i32 @_Z12call_virtualP5VBaseMS_FivE(ptr noundef %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
// LLVM:   extractvalue { i64, i64 } %{{.*}}, 1
// LLVM:   extractvalue { i64, i64 } %{{.*}}, 0
// LLVM:   and i64 %{{.*}}, 1
// LLVM:   icmp eq i64 %{{.*}}, 1
// LLVM:   phi ptr

// OGCG: define {{.*}} i32 @_Z12call_virtualP5VBaseMS_FivE(ptr {{.*}} %b, i64 %ptr.coerce0, i64 %ptr.coerce1)
// OGCG:   %memptr.adj = extractvalue { i64, i64 } %{{.*}}, 1
// OGCG:   %memptr.ptr = extractvalue { i64, i64 } %{{.*}}, 0
// OGCG:   %memptr.isvirtual = icmp ne i64 %{{.*}}, 0
// OGCG:   phi ptr
