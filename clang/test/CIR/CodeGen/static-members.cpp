// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s -check-prefix=CIR --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck %s -check-prefix=LLVM --input-file=%t-cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck %s -check-prefix=OGCG --input-file=%t.ll

struct HasDtor {
  ~HasDtor();
};
struct S {
  static inline HasDtor hd;
};

// CIR: cir.global linkonce_odr comdat @_ZN1S2hdE = #cir.zero : !rec_HasDtor {alignment = 1 : i64, {{.*}}comdat_name = "_ZN1S2hdE"}

// CIR: cir.func no_inline comdat internal private @__cxx_global_var_init() section(".text.startup"){{.*}}comdat_name = "_ZN1S2hdE"{{.*}}{
// CIR:   %[[GUARD_ADDR:.*]] = cir.get_global @_ZGVN1S2hdE : !cir.ptr<!u64i>
// CIR:   %[[GUARD_PTR:.*]] = cir.cast bitcast %[[GUARD_ADDR]] : !cir.ptr<!u64i> -> !cir.ptr<!u8i>
// CIR:   %[[GUARD_VAL:.*]] = cir.load align(8) atomic(acquire) %[[GUARD_PTR]] : !cir.ptr<!u8i>, !u8i
// CIR:   %[[ZERO_U8:.*]] = cir.const #cir.int<0> : !u8i
// CIR:   %[[NEEDS_INIT:.*]] = cir.cmp(eq, %[[GUARD_VAL]], %[[ZERO_U8]]) : !u8i, !cir.bool
// CIR:   cir.if %[[NEEDS_INIT]] {
// CIR:     %[[ACQ:.*]] = cir.call @__cxa_guard_acquire(%[[GUARD_ADDR]]) nothrow
// CIR:     %[[ZERO_S32:.*]] = cir.const #cir.int<0> : !s32i
// CIR:     %[[ACQUIRED:.*]] = cir.cmp(ne, %[[ACQ]], %[[ZERO_S32]]) : !s32i, !cir.bool
// CIR:     cir.if %[[ACQUIRED]] {
// CIR:       %[[HD:.*]] = cir.get_global @_ZN1S2hdE : !cir.ptr<!rec_HasDtor>
// CIR:       %[[DTOR:.*]] = cir.get_global @_ZN7HasDtorD1Ev : !cir.ptr<!cir.func<(!cir.ptr<!rec_HasDtor>)>>
// CIR:       %[[DTOR_CAST:.*]] = cir.cast bitcast %[[DTOR]] : !cir.ptr<!cir.func<(!cir.ptr<!rec_HasDtor>)>> -> !cir.ptr<!cir.func<(!cir.ptr<!void>)>>
// CIR:       %[[HD_CAST:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!rec_HasDtor> -> !cir.ptr<!void>
// CIR:       cir.call @__cxa_atexit(%[[DTOR_CAST]], %[[HD_CAST]], %{{.*}})
// CIR:       cir.call @__cxa_guard_release(%[[GUARD_ADDR]]) nothrow

// CIR: cir.global linkonce_odr comdat @_ZGVN1S2hdE = #cir.int<0> : !u64i {alignment = 8 : i64, comdat_name = "_ZN1S2hdE"}

// LLVM: @_ZN1S2hdE = linkonce_odr global %struct.HasDtor zeroinitializer, comdat
// LLVM: @_ZGVN1S2hdE = linkonce_odr global i64 0, comdat($_ZN1S2hdE)
// LLVM: @_ZN5Outer5Inner2hdE = linkonce_odr global %struct.HasDtor zeroinitializer, comdat
// LLVM: @_ZGVN5Outer5Inner2hdE = linkonce_odr global i64 0, comdat($_ZN5Outer5Inner2hdE)

// LLVM: @llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [
// LLVM-SAME:      { i32, ptr, ptr } { i32 65535, ptr @__cxx_global_var_init, ptr @_ZN1S2hdE },
// LLVM-SAME:      { i32, ptr, ptr } { i32 65535, ptr @__cxx_global_var_init.1, ptr @_ZN5Outer5Inner2hdE }]

// LLVM: @llvm.used = appending global [2 x ptr] [ptr @_ZN1S2hdE, ptr @_ZN5Outer5Inner2hdE]

// LLVM: define internal void @__cxx_global_var_init() {{.*}}comdat($_ZN1S2hdE)
// LLVM:   %[[GUARD_VAL:.*]] = load atomic i8, ptr @_ZGVN1S2hdE acquire, align 8
// LLVM:   %[[NEEDS_INIT:.*]] = icmp eq i8 %[[GUARD_VAL]], 0
// LLVM:   br i1 %[[NEEDS_INIT]], label %[[ACQUIRE_BB:.*]], label %[[END_BB:.*]]
// LLVM: [[ACQUIRE_BB]]:
// LLVM:   %[[ACQ:.*]] = call i32 @__cxa_guard_acquire(ptr @_ZGVN1S2hdE)
// LLVM:   %[[ACQUIRED:.*]] = icmp ne i32 %[[ACQ]], 0
// LLVM:   br i1 %[[ACQUIRED]], label %[[INIT_BB:.*]], label %[[MERGE_BB:.*]]
// LLVM: [[INIT_BB]]:
// LLVM:   %{{.*}} = call i32 @__cxa_atexit(ptr @_ZN7HasDtorD1Ev, ptr @_ZN1S2hdE, ptr @__dso_handle)
// LLVM:   call void @__cxa_guard_release(ptr @_ZGVN1S2hdE)

// OGCG: @_ZN1S2hdE = linkonce_odr global %struct.HasDtor zeroinitializer, comdat
// OGCG: @_ZGVN1S2hdE = linkonce_odr global i64 0, comdat($_ZN1S2hdE)
// OGCG: @_ZN5Outer5Inner2hdE = linkonce_odr global %struct.HasDtor zeroinitializer, comdat
// OGCG: @_ZGVN5Outer5Inner2hdE = linkonce_odr global i64 0, comdat($_ZN5Outer5Inner2hdE)
// OGCG: @llvm.global_ctors = appending global [2 x { i32, ptr, ptr }] [
// OGCG-SAME:      { i32, ptr, ptr } { i32 65535, ptr @__cxx_global_var_init, ptr @_ZN1S2hdE },
// OGCG-SAME:      { i32, ptr, ptr } { i32 65535, ptr @__cxx_global_var_init.1, ptr @_ZN5Outer5Inner2hdE }]

// OGCG: define internal void @__cxx_global_var_init() {{.*}} section ".text.startup" comdat($_ZN1S2hdE) {
// OGCG:   %[[GUARD:.*]] = load atomic i8, ptr @_ZGVN1S2hdE acquire
// OGCG:   %[[UNINIT:.*]] = icmp eq i8 %[[GUARD]], 0
// OGCG:   br i1 %[[UNINIT]], label %[[INIT_CHECK:.*]], label %[[INIT_END:.*]]
// OGCG: [[INIT_CHECK:.*]]:
// OGCG:   %[[GUARD_ACQUIRE:.*]] = call i32 @__cxa_guard_acquire(ptr @_ZGVN1S2hdE)
// OGCG:   %[[TOBOOL:.*]] = icmp ne i32 %[[GUARD_ACQUIRE]], 0
// OGCG:   br i1 %[[TOBOOL]], label %[[INIT:.*]], label %[[INIT_END]]
// OGCG: [[INIT:.*]]:
// OGCG:   %[[ATEXIT:.*]] = call i32 @__cxa_atexit(ptr @_ZN7HasDtorD1Ev, ptr @_ZN1S2hdE, ptr @__dso_handle)
// OGCG:   call void @__cxa_guard_release(ptr @_ZGVN1S2hdE)
// OGCG:   br label %[[INIT_END]]
// OGCG: [[INIT_END]]:

struct Outer {
  struct Inner {
    static inline HasDtor hd;
  };
};

// CIR: cir.global linkonce_odr comdat @_ZN5Outer5Inner2hdE = #cir.zero : !rec_HasDtor {alignment = 1 : i64, {{.*}}comdat_name = "_ZN5Outer5Inner2hdE"}

// CIR: cir.func no_inline comdat internal private @__cxx_global_var_init.1() section(".text.startup"){{.*}}comdat_name = "_ZN5Outer5Inner2hdE"
// CIR:   %[[GUARD_ADDR:.*]] = cir.get_global @_ZGVN5Outer5Inner2hdE : !cir.ptr<!u64i>
// CIR:   %[[GUARD_PTR:.*]] = cir.cast bitcast %[[GUARD_ADDR]] : !cir.ptr<!u64i> -> !cir.ptr<!u8i>
// CIR:   %[[GUARD_VAL:.*]] = cir.load align(8) atomic(acquire) %[[GUARD_PTR]] : !cir.ptr<!u8i>, !u8i
// CIR:   %[[ZERO_U8:.*]] = cir.const #cir.int<0> : !u8i
// CIR:   %[[NEEDS_INIT:.*]] = cir.cmp(eq, %[[GUARD_VAL]], %[[ZERO_U8]]) : !u8i, !cir.bool
// CIR:   cir.if %[[NEEDS_INIT]] {
// CIR:     %[[ACQ:.*]] = cir.call @__cxa_guard_acquire(%[[GUARD_ADDR]]) nothrow
// CIR:     %[[ZERO_S32:.*]] = cir.const #cir.int<0> : !s32i
// CIR:     %[[ACQUIRED:.*]] = cir.cmp(ne, %[[ACQ]], %[[ZERO_S32]]) : !s32i, !cir.bool
// CIR:     cir.if %[[ACQUIRED]] {
// CIR:       %[[HD:.*]] = cir.get_global @_ZN5Outer5Inner2hdE : !cir.ptr<!rec_HasDtor>
// CIR:       %[[DTOR:.*]] = cir.get_global @_ZN7HasDtorD1Ev : !cir.ptr<!cir.func<(!cir.ptr<!rec_HasDtor>)>>
// CIR:       %[[DTOR_CAST:.*]] = cir.cast bitcast %[[DTOR]] : !cir.ptr<!cir.func<(!cir.ptr<!rec_HasDtor>)>> -> !cir.ptr<!cir.func<(!cir.ptr<!void>)>>
// CIR:       %[[HD_CAST:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!rec_HasDtor> -> !cir.ptr<!void>
// CIR:       cir.call @__cxa_atexit(%[[DTOR_CAST]], %[[HD_CAST]], %{{.*}}) nothrow
// CIR:       cir.call @__cxa_guard_release(%[[GUARD_ADDR]]) nothrow

// CIR: cir.global linkonce_odr comdat @_ZGVN5Outer5Inner2hdE = #cir.int<0> : !u64i {alignment = 8 : i64, comdat_name = "_ZN5Outer5Inner2hdE"}

// LLVM: define internal void @__cxx_global_var_init.1() {{.*}}comdat($_ZN5Outer5Inner2hdE)
// LLVM:   %[[GUARD_VAL:.*]] = load atomic i8, ptr @_ZGVN5Outer5Inner2hdE acquire, align 8
// LLVM:   %[[NEEDS_INIT:.*]] = icmp eq i8 %[[GUARD_VAL]], 0
// LLVM:   br i1 %[[NEEDS_INIT]], label %[[ACQUIRE_BB:.*]], label %[[END_BB:.*]]
// LLVM: [[ACQUIRE_BB]]:
// LLVM:   %[[ACQ:.*]] = call i32 @__cxa_guard_acquire(ptr @_ZGVN5Outer5Inner2hdE)
// LLVM:   %[[ACQUIRED:.*]] = icmp ne i32 %[[ACQ]], 0
// LLVM:   br i1 %[[ACQUIRED]], label %[[INIT_BB:.*]], label %[[MERGE_BB:.*]]
// LLVM: [[INIT_BB]]:
// LLVM:   %{{.*}} = call i32 @__cxa_atexit(ptr @_ZN7HasDtorD1Ev, ptr @_ZN5Outer5Inner2hdE, ptr @__dso_handle)
// LLVM:   call void @__cxa_guard_release(ptr @_ZGVN5Outer5Inner2hdE)

// OGCG: define internal void @__cxx_global_var_init.1() {{.*}} section ".text.startup" comdat($_ZN5Outer5Inner2hdE) {
// OGCG:   %[[GUARD:.*]] = load atomic i8, ptr @_ZGVN5Outer5Inner2hdE acquire
// OGCG:   %[[UNINIT:.*]] = icmp eq i8 %[[GUARD]], 0
// OGCG:   br i1 %[[UNINIT]], label %[[INIT_CHECK:.*]], label %[[INIT_END:.*]]
// OGCG: [[INIT_CHECK:.*]]:
// OGCG:   %[[GUARD_ACQUIRE:.*]] = call i32 @__cxa_guard_acquire(ptr @_ZGVN5Outer5Inner2hdE)
// OGCG:   %[[TOBOOL:.*]] = icmp ne i32 %[[GUARD_ACQUIRE]], 0
// OGCG:   br i1 %[[TOBOOL]], label %[[INIT:.*]], label %[[INIT_END]]
// OGCG: [[INIT:.*]]:
// OGCG:   %[[ATEXIT:.*]] = call i32 @__cxa_atexit(ptr @_ZN7HasDtorD1Ev, ptr @_ZN5Outer5Inner2hdE, ptr @__dso_handle)
// OGCG:   call void @__cxa_guard_release(ptr @_ZGVN5Outer5Inner2hdE)
// OGCG:   br label %[[INIT_END]]
// OGCG: [[INIT_END]]:


// No _GLOBAL__sub_I_ is generated because all inits are comdat-keyed.
