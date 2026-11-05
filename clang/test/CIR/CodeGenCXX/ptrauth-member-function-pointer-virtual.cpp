// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -fclangir -emit-cir -std=c++11 -O1 -disable-llvm-passes -o - %s \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -fclangir -emit-llvm -std=c++11 -O1 -disable-llvm-passes -o - %s \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -fclangir -emit-llvm -std=c++11 -O1 -disable-llvm-passes -stack-protector 1 -o - %s \
// RUN:   | FileCheck %s --check-prefix=STACK-PROT

namespace std {
template <typename T> class initializer_list {
  const T *__begin_;
  __SIZE_TYPE__ __size_;

  initializer_list(const T *, __SIZE_TYPE__);
};
} // namespace std

struct Base0 {
  void nonvirtual0();
  virtual void virtual1();
};

struct Derived0 : Base0 {
  void virtual1() override;
  void nonvirtual5();
};

typedef void (Base0::*MethodTy0)();
typedef void (Derived0::*MethodTy1)();
typedef void (Base0::*VariadicMethodTy0)(int, ...);

struct Class0 {
  MethodTy1 m0;
};

MethodTy1 gmethod0 = reinterpret_cast<MethodTy1>(&Base0::nonvirtual0);
MethodTy0 gmethod1 = reinterpret_cast<MethodTy0>(&Derived0::virtual1);

void initList(std::initializer_list<MethodTy1>);

void testConversion4() {
  MethodTy0 method0 = reinterpret_cast<MethodTy0>(&Derived0::virtual1);
}

void testConvertNull() {
  VariadicMethodTy0 t = (VariadicMethodTy0)(MethodTy0{});
}

namespace TestAnonymousNamespace {
namespace {
struct S {
  virtual void foo() {}
};
} // namespace

void test() {
  auto t = &S::foo;
}
} // namespace TestAnonymousNamespace

void testArrayInit() {
  MethodTy1 p0[] = {&Base0::nonvirtual0};
  MethodTy1 p1[] = {&Base0::virtual1};
  Class0 c0{&Base0::nonvirtual0};
  Class0 c1{&Base0::virtual1};
  initList({&Base0::nonvirtual0});
  initList({&Base0::virtual1});
}

namespace testNoexceptConversion {
struct S {
  void nonvirtual_noexcept() noexcept;
  virtual void virtual_noexcept() noexcept;
};

void (S::*mfptr1)() = &S::nonvirtual_noexcept;
void (S::*mfptr2)() = &S::virtual_noexcept;

void test0() {
  void (S::*p0)() = &S::nonvirtual_noexcept;
}

void test1() {
  void (S::*p0)() = &S::virtual_noexcept;
}
} // namespace testNoexceptConversion

// CIR-DAG: @gmethod0 = #cir.const_record<{#cir.global_view<@_ZN5Base011nonvirtual0Ev> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct {alignment = 8 : i64, cir.member_func_ptr_ptrauth = [{{\[}}35591{{\]}}]}
// CIR-DAG: @gmethod1 = #cir.const_record<{#cir.global_view<@_ZN5Base08virtual1Ev_vfpthunk_> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct {alignment = 8 : i64, cir.member_func_ptr_ptrauth = [{{\[}}22163{{\]}}]}
// CIR-DAG: @__const._Z13testArrayInitv.p1 = #cir.const_array<[#cir.const_record<{#cir.global_view<@_ZN5Base08virtual1Ev_vfpthunk_> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct]> : !cir.array<!rec_anon_struct x 1> {alignment = 8 : i64, cir.member_func_ptr_ptrauth = [{{\[}}0 : i32, 35591{{\]}}]
// CIR-DAG: @__const._Z13testArrayInitv.c1 = #cir.const_record<{#cir.const_record<{#cir.global_view<@_ZN5Base08virtual1Ev_vfpthunk_> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct}> : !rec_Class0 {alignment = 8 : i64, cir.member_func_ptr_ptrauth = [{{\[}}0 : i32, 35591{{\]}}]
// CIR-DAG: @_ZN22testNoexceptConversion6mfptr2E = #cir.const_record<{#cir.global_view<@_ZN22testNoexceptConversion1S16virtual_noexceptEv_vfpthunk_> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct {alignment = 8 : i64, cir.member_func_ptr_ptrauth = [{{\[}}54461{{\]}}]}
// CIR-LABEL: cir.func @_Z15testConversion4v()
// CIR: %[[METHOD0:.*]] = cir.alloca !rec_anon_struct
// CIR: %[[VIRTUAL_CONST:.*]] = cir.const #cir.const_record<{#cir.global_view<@_ZN5Base08virtual1Ev_vfpthunk_> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct {cir.member_func_ptr_ptrauth = [{{\[}}22163{{\]}}]}
// CIR-NOT: cir.call_llvm_intrinsic "ptrauth.resign"
// CIR: cir.store align(8) %[[VIRTUAL_CONST]], %[[METHOD0]]
// CIR-LABEL: cir.func @_Z15testConvertNullv()
// CIR: %[[NULL_METHOD:.*]] = cir.const #cir.const_record<{#cir.int<0> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct
// CIR-NOT: cir.call_llvm_intrinsic "ptrauth.resign"
// CIR: cir.store align(8) %[[NULL_METHOD]], %{{.*}}
// CIR-LABEL: cir.func @_Z13testArrayInitv()
// CIR: %[[NONVIRTUAL_INITLIST_LOCAL:.*]] = cir.const #cir.const_record<{#cir.global_view<@_ZN5Base011nonvirtual0Ev> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct {cir.member_func_ptr_ptrauth = [{{\[}}35591{{\]}}]}
// CIR-NOT: cir.call_llvm_intrinsic "ptrauth.resign"
// CIR: cir.store align(8) %[[NONVIRTUAL_INITLIST_LOCAL]], %{{.*}}
// CIR: %[[VIRTUAL_INITLIST_LOCAL:.*]] = cir.const #cir.const_record<{#cir.global_view<@_ZN5Base08virtual1Ev_vfpthunk_> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct {cir.member_func_ptr_ptrauth = [{{\[}}35591{{\]}}]}
// CIR-NOT: cir.call_llvm_intrinsic "ptrauth.resign"
// CIR: cir.store align(8) %[[VIRTUAL_INITLIST_LOCAL]], %{{.*}}
// CIR-LABEL: cir.func @_ZN22testNoexceptConversion5test1Ev()
// CIR: %[[NOEXCEPT_LOCAL:.*]] = cir.const #cir.const_record<{#cir.global_view<@_ZN22testNoexceptConversion1S16virtual_noexceptEv_vfpthunk_> : !s64i, #cir.int<0> : !s64i}> : !rec_anon_struct {cir.member_func_ptr_ptrauth = [{{\[}}54461{{\]}}]}
// CIR: cir.store align(8) %[[NOEXCEPT_LOCAL]], %{{.*}}

// LLVM-DAG: @gmethod0 = global { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base011nonvirtual0Ev, i32 0, i64 35591) to i64), i64 0 }, align 8
// LLVM-DAG: @gmethod1 = global { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base08virtual1Ev_vfpthunk_, i32 0, i64 22163) to i64), i64 0 }, align 8
// LLVM-DAG: @__const._Z13testArrayInitv.p1 = private unnamed_addr constant [1 x { i64, i64 }] [{ i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base08virtual1Ev_vfpthunk_, i32 0, i64 35591) to i64), i64 0 }], align 8
// LLVM-DAG: @__const._Z13testArrayInitv.c1 = private unnamed_addr constant %struct.Class0 { { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base08virtual1Ev_vfpthunk_, i32 0, i64 35591) to i64), i64 0 } }, align 8
// LLVM-DAG: @_ZN22testNoexceptConversion6mfptr2E = global { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN22testNoexceptConversion1S16virtual_noexceptEv_vfpthunk_, i32 0, i64 54461) to i64), i64 0 }, align 8
// LLVM-LABEL: define{{.*}} void @_Z15testConversion4v()
// LLVM: store { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base08virtual1Ev_vfpthunk_, i32 0, i64 22163) to i64), i64 0 }, ptr %{{.*}}, align 8
// LLVM-NOT: @llvm.ptrauth.resign
// LLVM-LABEL: define{{.*}} void @_Z15testConvertNullv()
// LLVM: store { i64, i64 } zeroinitializer, ptr %{{.*}}, align 8
// LLVM-NOT: @llvm.ptrauth.resign
// LLVM: define internal void @_ZN22TestAnonymousNamespace12_GLOBAL__N_11S3fooEv_vfpthunk_(
// LLVM-LABEL: define{{.*}} void @_Z13testArrayInitv()
// LLVM: store { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base011nonvirtual0Ev, i32 0, i64 35591) to i64), i64 0 }, ptr %{{.*}}, align 8
// LLVM-NOT: @llvm.ptrauth.resign
// LLVM: store { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN5Base08virtual1Ev_vfpthunk_, i32 0, i64 35591) to i64), i64 0 }, ptr %{{.*}}, align 8
// LLVM-NOT: @llvm.ptrauth.resign
// LLVM-LABEL: define{{.*}} void @_ZN22testNoexceptConversion5test1Ev()
// LLVM: store { i64, i64 } { i64 ptrtoint (ptr ptrauth (ptr @_ZN22testNoexceptConversion1S16virtual_noexceptEv_vfpthunk_, i32 0, i64 54461) to i64), i64 0 }, ptr %{{.*}}, align 8

// STACK-PROT: define {{.*}}_vfpthunk{{.*}}[[THUNK_ATTRS:#[0-9]+]]
// STACK-PROT: attributes [[THUNK_ATTRS]] =
// STACK-PROT-NOT: ssp
// STACK-PROT-NOT: sspstrong
// STACK-PROT-NOT: sspreq
// STACK-PROT-NEXT: attributes
