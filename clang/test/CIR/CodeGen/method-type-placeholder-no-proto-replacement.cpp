// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -clangir-disable-passes -emit-cir %s -o - | FileCheck %s

// Regression test: when a function template taking a member
// function pointer is first seen with an incomplete member-function return type,
// CIR may create a placeholder no-proto declaration. Once the real declaration
// is available, replacement must reconcile MethodType operands (not only
// pointer operands), otherwise call verification fails with a stale
// !cir.method<!cir.func<()>> argument.
//
// Also ensure this declaration-type update path does not rely on
// SymbolTable-based use queries by keeping duplicate weak alias declarations in
// the TU (transient non-unique symbols).

extern "C" void __weak_impl();
extern "C" void __weak_impl() {}
#pragma weak weak_alias = __weak_impl
#pragma weak weak_alias = __weak_impl

struct Ret;
struct Obj {
  Ret m(int);
};
extern int helper(int);
extern int helper2(int);
extern int *gCounter;
extern int gFlag;

template <typename C, typename R, typename... A>
int wrap(R (C::*fn)(A...));

int use(Obj *o) {
  (void)o;
  // Keep unrelated symbol uses in this TU so applyReplacements must
  // ignore non-replaced names while still fixing placeholder method types.
  if (gCounter && gFlag)
    return helper(wrap(&Obj::m));
  return helper2(wrap(&Obj::m));
}

struct Ret {
  int x;
};

Ret Obj::m(int v) { return {v}; }

template <typename C, typename R, typename... A>
int wrap(R (C::*fn)(A...)) {
  return fn ? 1 : 0;
}

// CHECK-COUNT-1: cir.alias {{.*}} @{{.*weak_alias.*}} = @__weak_impl
// CHECK-LABEL: @_Z3useP3Obj
// CHECK: cir.get_global @gCounter : !cir.ptr<!cir.ptr<!s32i>>
// CHECK: cir.get_global @gFlag : !cir.ptr<!s32i>
// CHECK: %[[M:.*]] = cir.const #cir.method<@_ZN3Obj1mEi>
// CHECK-SAME: !cir.method<!cir.func<(!s32i) -> !rec_Ret> in !rec_Obj>
// CHECK: cir.cast bitcast %[[M]] : !cir.method<!cir.func<(!s32i) -> !rec_Ret> in !rec_Obj> -> !cir.method<!cir.func<()> in !rec_Obj>
// CHECK: cir.cast bitcast %{{.*}} : !cir.method<!cir.func<()> in !rec_Obj> -> !cir.method<!cir.func<(!s32i) -> !rec_Ret> in !rec_Obj>
// CHECK: cir.call @_Z6helperi(%{{.*}}) : (!s32i) -> !s32i
// CHECK: cir.call @_Z4wrapI3Obj3RetJiEEiMT_FT0_DpT1_E(%{{.*}})
// CHECK: cir.call @_Z7helper2i(%{{.*}}) : (!s32i) -> !s32i
