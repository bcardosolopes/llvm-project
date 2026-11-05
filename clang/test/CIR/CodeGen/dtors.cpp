// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -mconstructor-aliases -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -mconstructor-aliases -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// Classes whose structors have no virtual bases get their complete-object
// variant folded into the base one, so calls below target C2/D2 just like in
// OGCG. Derived, which has a virtual base, keeps a distinct D1.

struct A {
  ~A();
};

void test_temporary_dtor() {
  A();
}

// A::~A() is only declared here, so there is nothing to fold and the call keeps
// targeting the complete-object variant.

// CIR: cir.func {{.*}} @_Z19test_temporary_dtorv()
// CIR:   %[[ALLOCA:.*]] = cir.alloca !rec_A, !cir.ptr<!rec_A>, ["agg.tmp.ensured"]
// CIR:   cir.call @_ZN1AD1Ev(%[[ALLOCA]]) nothrow : (!cir.ptr<!rec_A>) -> ()

// LLVM: define dso_local void @_Z19test_temporary_dtorv(){{.*}}
// LLVM:   %[[ALLOCA:.*]] = alloca %struct.A, i64 1, align 1
// LLVM:   call void @_ZN1AD1Ev(ptr {{[^%]*}}%[[ALLOCA]])

// OGCG: define dso_local void @_Z19test_temporary_dtorv()
// OGCG:   %[[ALLOCA:.*]] = alloca %struct.A, align 1
// OGCG:   call void @_ZN1AD1Ev(ptr {{.*}} %[[ALLOCA]])

struct B {
  int n;
  B(int n) : n(n) {}
  ~B() {}
};

bool make_temp(const B &) { return false; }
bool test_temp_or() { return make_temp(1) || make_temp(2); }

// CIR: cir.func{{.*}} @_Z12test_temp_orv()
// CIR:   %[[REF_TMP1:.*]] = cir.alloca !rec_B, !cir.ptr<!rec_B>, ["ref.tmp1"]
// CIR:   %[[CLEANUP_COND:.*]] = cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]
// CIR:   cir.scope {
// CIR:     %[[REF_TMP0:.*]] = cir.alloca !rec_B, !cir.ptr<!rec_B>, ["ref.tmp0"]
// CIR:     %[[ONE:.*]] = cir.const #cir.int<1>
// CIR:     cir.call @_ZN1BC2Ei(%[[REF_TMP0]], %[[ONE]])
// CIR:     %[[MAKE_TEMP0:.*]] = cir.call @_Z9make_tempRK1B(%[[REF_TMP0]])
// CIR:     %[[TERNARY:.*]] = cir.ternary(%[[MAKE_TEMP0]], true {
// CIR:       %[[TRUE:.*]] = cir.const #true
// CIR:       cir.yield %[[TRUE]] : !cir.bool
// CIR:     }, false {
// CIR:       cir.call @_ZN1BC2Ei(%[[REF_TMP1]],
// CIR:       cir.call @_Z9make_tempRK1B(%[[REF_TMP1]])
// CIR:       cir.yield
// CIR:     })
// CIR:     cir.if
// CIR:       cir.call @_ZN1BD2Ev(%[[REF_TMP1]])
// CIR:     }
// CIR:     cir.call @_ZN1BD2Ev(%[[REF_TMP0]])
// CIR:   }

// Verify conditional cleanup pattern in LLVM IR: the destructor for
// ref_tmp1 runs conditionally (guarded by cleanup.cond flag), while
// ref_tmp0's destructor runs unconditionally after the ternary.
// LLVM: define{{.*}} i1 @_Z12test_temp_orv(){{.*}} {
// LLVM:   call void @_ZN1BC2Ei({{.*}} i32 noundef 1)
// LLVM:   call {{.*}} @_Z9make_tempRK1B
// LLVM:   br i1
// LLVM:   call void @_ZN1BC2Ei({{.*}} i32 noundef 2)
// LLVM:   store i8 1
// LLVM:   call {{.*}} @_Z9make_tempRK1B
// LLVM:   br i1
// LLVM:   call void @_ZN1BD2Ev
// LLVM:   call void @_ZN1BD2Ev

// OGCG: define {{.*}} i1 @_Z12test_temp_orv()
// OGCG: [[ENTRY:.*]]:
// OGCG:   %[[RETVAL:.*]] = alloca i1
// OGCG:   %[[REF_TMP0:.*]] = alloca %struct.B
// OGCG:   %[[REF_TMP1:.*]] = alloca %struct.B
// OGCG:   %[[CLEANUP_COND:.*]] = alloca i1
// OGCG:   call void @_ZN1BC2Ei(ptr {{.*}} %[[REF_TMP0]], i32 {{.*}} 1)
// OGCG:   %[[MAKE_TEMP0:.*]] = call {{.*}} i1 @_Z9make_tempRK1B(ptr {{.*}} %[[REF_TMP0]])
// OGCG:   store i1 false, ptr %cleanup.cond
// OGCG:   br i1 %[[MAKE_TEMP0]], label %[[LOR_END:.*]], label %[[LOR_RHS:.*]]
// OGCG: [[LOR_RHS]]:
// OGCG:   call void @_ZN1BC2Ei(ptr {{.*}} %[[REF_TMP1]], i32 {{.*}} 2)
// OGCG:   store i1 true, ptr %[[CLEANUP_COND]]
// OGCG:   %[[MAKE_TEMP1:.*]] = call {{.*}} i1 @_Z9make_tempRK1B(ptr {{.*}} %[[REF_TMP1]])
// OGCG:   br label %[[LOR_END]]
// OGCG: [[LOR_END]]:
// OGCG:    %[[PHI:.*]] = phi i1 [ true, %[[ENTRY]] ], [ %[[MAKE_TEMP1]], %[[LOR_RHS]] ]
// OGCG:   store i1 %[[PHI]], ptr %[[RETVAL]]
// OGCG:   %[[CLEANUP_IS_ACTIVE:.*]] = load i1, ptr %[[CLEANUP_COND]]
// OGCG:   br i1 %[[CLEANUP_IS_ACTIVE]], label %[[CLEANUP_ACTION:.*]], label %[[CLEANUP_DONE:.*]]
// OGCG: [[CLEANUP_ACTION]]:
// OGCG:   call void @_ZN1BD2Ev(ptr {{.*}} %[[REF_TMP1]])
// OGCG:   br label %[[CLEANUP_DONE]]
// OGCG: [[CLEANUP_DONE]]:
// OGCG:   call void @_ZN1BD2Ev(ptr {{.*}} %[[REF_TMP0]])

bool test_temp_and() { return make_temp(1) && make_temp(2); }

// CIR: cir.func{{.*}} @_Z13test_temp_andv()
// CIR:   %[[REF_TMP1:.*]] = cir.alloca !rec_B, !cir.ptr<!rec_B>, ["ref.tmp1"]
// CIR:   %[[CLEANUP_COND:.*]] = cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]
// CIR:   cir.scope {
// CIR:     %[[REF_TMP0:.*]] = cir.alloca !rec_B, !cir.ptr<!rec_B>, ["ref.tmp0"]
// CIR:     %[[ONE:.*]] = cir.const #cir.int<1>
// CIR:     cir.call @_ZN1BC2Ei(%[[REF_TMP0]], %[[ONE]])
// CIR:     %[[MAKE_TEMP0:.*]] = cir.call @_Z9make_tempRK1B(%[[REF_TMP0]])
// CIR:     %[[TERNARY:.*]] = cir.ternary(%[[MAKE_TEMP0]], true {
// CIR:       cir.call @_ZN1BC2Ei(%[[REF_TMP1]],
// CIR:       cir.call @_Z9make_tempRK1B(%[[REF_TMP1]])
// CIR:       cir.yield
// CIR:     }, false {
// CIR:       %[[FALSE:.*]] = cir.const #false
// CIR:       cir.yield %[[FALSE]] : !cir.bool
// CIR:     })
// CIR:     cir.if
// CIR:       cir.call @_ZN1BD2Ev(%[[REF_TMP1]])
// CIR:     }
// CIR:     cir.call @_ZN1BD2Ev(%[[REF_TMP0]])
// CIR:   }

// LLVM: define{{.*}} i1 @_Z13test_temp_andv(){{.*}} {
// LLVM:   call void @_ZN1BC2Ei({{.*}} i32 noundef 1)
// LLVM:   call {{.*}} @_Z9make_tempRK1B
// LLVM:   br i1
// LLVM:   call void @_ZN1BC2Ei({{.*}} i32 noundef 2)
// LLVM:   store i8 1
// LLVM:   call {{.*}} @_Z9make_tempRK1B
// LLVM:   br i1
// LLVM:   call void @_ZN1BD2Ev
// LLVM:   call void @_ZN1BD2Ev

// OGCG: define {{.*}} i1 @_Z13test_temp_andv()
// OGCG: [[ENTRY:.*]]:
// OGCG:   %[[RETVAL:.*]] = alloca i1
// OGCG:   %[[REF_TMP0:.*]] = alloca %struct.B
// OGCG:   %[[REF_TMP1:.*]] = alloca %struct.B
// OGCG:   %[[CLEANUP_COND:.*]] = alloca i1
// OGCG:   call void @_ZN1BC2Ei(ptr {{.*}} %[[REF_TMP0]], i32 {{.*}} 1)
// OGCG:   %[[MAKE_TEMP0:.*]] = call {{.*}} i1 @_Z9make_tempRK1B(ptr {{.*}} %[[REF_TMP0]])
// OGCG:   store i1 false, ptr %cleanup.cond
// OGCG:   br i1 %[[MAKE_TEMP0]], label %[[LAND_RHS:.*]], label %[[LAND_END:.*]]
// OGCG: [[LAND_RHS]]:
// OGCG:   call void @_ZN1BC2Ei(ptr {{.*}} %[[REF_TMP1]], i32 {{.*}} 2)
// OGCG:   store i1 true, ptr %[[CLEANUP_COND]]
// OGCG:   %[[MAKE_TEMP1:.*]] = call {{.*}} i1 @_Z9make_tempRK1B(ptr {{.*}} %[[REF_TMP1]])
// OGCG:   br label %[[LAND_END]]
// OGCG: [[LAND_END]]:
// OGCG:   %[[PHI:.*]] = phi i1 [ false, %[[ENTRY]] ], [ %[[MAKE_TEMP1]], %[[LAND_RHS]] ]
// OGCG:   store i1 %[[PHI]], ptr %[[RETVAL]]
// OGCG:   %[[CLEANUP_IS_ACTIVE:.*]] = load i1, ptr %[[CLEANUP_COND]]
// OGCG:   br i1 %[[CLEANUP_IS_ACTIVE]], label %[[CLEANUP_ACTION:.*]], label %[[CLEANUP_DONE:.*]]
// OGCG: [[CLEANUP_ACTION]]:
// OGCG:   call void @_ZN1BD2Ev(ptr {{.*}} %[[REF_TMP1]])
// OGCG:   br label %[[CLEANUP_DONE]]
// OGCG: [[CLEANUP_DONE]]:
// OGCG:   call void @_ZN1BD2Ev(ptr {{.*}} %[[REF_TMP0]])

struct C {
  ~C();
};

struct D {
  int n;
  C c;
  ~D() {}
};

void test_nested_dtor() {
  D d;
}

// CIR: cir.func{{.*}} @_Z16test_nested_dtorv()
// CIR:   cir.call @_ZN1DD2Ev(%{{.*}})

// LLVM: define {{.*}} void @_Z16test_nested_dtorv(){{.*}}
// LLVM:   call void @_ZN1DD2Ev(ptr noundef nonnull align 4 dereferenceable(5) %{{.*}})

// OGCG: define {{.*}} void @_Z16test_nested_dtorv()
// OGCG:   call void @_ZN1DD2Ev(ptr {{.*}} %{{.*}})

// OGCG: define {{.*}} void @_ZN1DD2Ev
// OGCG:   %[[C:.*]] = getelementptr inbounds i8, ptr %{{.*}}, i64 4
// OGCG:   call void @_ZN1CD1Ev(ptr {{.*}} %[[C]])

struct E {
  ~E();
};

struct F : public E {
  int n;
  ~F() {}
};

void test_base_dtor_call() {
  F f;
}

// CIR: cir.func {{.*}} @_Z19test_base_dtor_callv()
// CIR:   cir.call @_ZN1FD2Ev(%{{.*}}) nothrow : (!cir.ptr<!rec_F>) -> ()

// LLVM: define {{.*}} void @_Z19test_base_dtor_callv(){{.*}}
// LLVM:   call void @_ZN1FD2Ev(ptr noundef nonnull align 4 dereferenceable(4) %{{.*}})

// OGCG: define {{.*}} void @_Z19test_base_dtor_callv()
// OGCG:   call void @_ZN1FD2Ev(ptr {{.*}} %{{.*}})

// OGCG: define {{.*}} void @_ZN1FD2Ev
// OGCG:   call void @_ZN1ED2Ev(ptr {{.*}} %{{.*}})

struct VirtualBase {
  ~VirtualBase();
};

struct Derived : virtual VirtualBase {
  ~Derived() {}
};

void test_base_dtor_call_virtual_base() {
  Derived d;
}

// Derived D2 (base) destructor -- does not call VirtualBase destructor

// CIR:     cir.func {{.*}} @_ZN7DerivedD2Ev
// CIR-NOT:   cir.call{{.*}} @_ZN11VirtualBaseD2Ev
// CIR:       cir.return

// LLVM:     define {{.*}} void @_ZN7DerivedD2Ev
// LLVM-NOT:   call{{.*}} @_ZN11VirtualBaseD2Ev
// LLVM:       ret

// Derived D1 (complete) destructor -- does call VirtualBase destructor. The
// virtual base rules out the fold, so D1 stays a real function here.

// CIR: cir.func {{.*}} @_ZN7DerivedD1Ev
// CIR:   %[[THIS:.*]] = cir.load %{{.*}}
// CIR:   %[[VTT:.*]] = cir.vtt.address_point @_ZTT7Derived, offset = 0 -> !cir.ptr<!cir.ptr<!void>>
// CIR:   cir.call @_ZN7DerivedD2Ev(%[[THIS]], %[[VTT]])
// CIR:   %[[VIRTUAL_BASE:.*]] = cir.base_class_addr %[[THIS]] : !cir.ptr<!rec_Derived> nonnull [0] -> !cir.ptr<!rec_VirtualBase>
// CIR:   cir.call @_ZN11VirtualBaseD2Ev(%[[VIRTUAL_BASE]])

// LLVM: define {{.*}} void @_ZN7DerivedD1Ev
// LLVM:   call void @_ZN7DerivedD2Ev(ptr noundef nonnull align 8 dereferenceable(8) %{{.*}}, ptr noundef @_ZTT7Derived)
// LLVM:   call void @_ZN11VirtualBaseD2Ev(ptr {{[^%]*}}%{{.*}})

// OGCG emits these destructors in reverse order

// OGCG: define {{.*}} void @_ZN7DerivedD1Ev
// OGCG:   call void @_ZN7DerivedD2Ev(ptr {{.*}} %{{.*}}, ptr {{.*}} @_ZTT7Derived)
// OGCG:   call void @_ZN11VirtualBaseD2Ev(ptr {{.*}} %{{.*}})

// OGCG:     define {{.*}} void @_ZN7DerivedD2Ev
// OGCG-NOT:   call{{.*}} @_ZN11VirtualBaseD2Ev
// OGCG:       ret

// The linkonce_odr destructor bodies come after every function that uses them.

// CIR: cir.func {{.*}} @_ZN1DD2Ev
// CIR:   %[[THIS:.*]] = cir.load %{{.*}}
// CIR:   %[[CAST:.*]] = cir.cast bitcast %[[THIS]] : !cir.ptr<!rec_D> -> !cir.ptr<!u8i>
// CIR:   %[[OFFSET:.*]] = cir.const #cir.int<4> : !s64i
// CIR:   %[[PTR:.*]] = cir.ptr_stride inbounds %[[CAST]], %[[OFFSET]]
// CIR:   %[[C:.*]] = cir.cast bitcast %[[PTR]] : !cir.ptr<!u8i> -> !cir.ptr<!rec_C>
// CIR:   cir.call @_ZN1CD1Ev(%[[C]])

// LLVM: define {{.*}} void @_ZN1DD2Ev
// LLVM:   %[[C:.*]] = getelementptr inbounds i8, ptr %{{.*}}, i64 4
// LLVM:   call void @_ZN1CD1Ev(ptr {{[^%]*}}%[[C]])

// Because E is at offset 0 in F, there is no getelementptr needed.

// CIR: cir.func {{.*}} @_ZN1FD2Ev
// CIR:   %[[BASE_E:.*]] = cir.base_class_addr %{{.*}} : !cir.ptr<!rec_F> nonnull [0] -> !cir.ptr<!rec_E>
// CIR:   cir.call @_ZN1ED2Ev(%[[BASE_E]]) nothrow : (!cir.ptr<!rec_E>) -> ()

// LLVM: define {{.*}} void @_ZN1FD2Ev
// LLVM:   call void @_ZN1ED2Ev(ptr {{[^%]*}}%{{.*}})
