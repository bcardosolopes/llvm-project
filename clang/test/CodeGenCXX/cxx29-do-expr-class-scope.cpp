// RUN: %clang_cc1 -std=c++2d -emit-llvm -triple x86_64-linux-gnu -O0 -disable-O0-optnone -o - %s | FileCheck %s

// Emitting a do-expression that lives in a default argument. A default
// argument is checked once, at the declaration, and its declarations are
// marked used from CheckCXXDefaultArgExpr via
// MarkDeclarationsReferencedInExpr(Init, /*SkipLocalVariables=*/true) -- the
// skip exists because an ordinary local belongs to a scope the caller is not
// entering. A do-expression body's locals are not like that: they are part of
// the default-argument expression itself, so if they are skipped nothing else
// marks them and CodeGen reaches an unmarked declaration:
//
//   Assertion `!isa<VarDecl>(D) || !cast<VarDecl>(D)->isLocalVarDecl()
//              || ...' -- "Should not use decl without marking it used!"

// The synthetic context is also what a local class declared in a body mangles
// as its enclosing function, so its name shows up in the module (see
// call_with_local_class below). Module-level globals come first, so check it
// here.
// CHECK: @__const._ZN14WithLocalClass14__do_expr_bodyEv.l = {{.*}} %struct.L { i32 9 }

struct S {
  static int f(int v = do { int j = 8; do_return j; }) { return v; }
};

// CHECK-LABEL: define {{.*}} @_Z18call_default_arg_1v
// The body local is emitted into the caller's frame, and its value flows out
// through the do-expression's result slot and into the call.
// CHECK: %[[RES:.*]] = alloca i32
// CHECK: %[[J:.*]] = alloca i32
// CHECK: store i32 8, ptr %[[J]]
// CHECK: %[[V:.*]] = load i32, ptr %[[J]]
// CHECK: store i32 %[[V]], ptr %[[RES]]
// CHECK: %[[ARG:.*]] = load i32, ptr %[[RES]]
// CHECK: call {{.*}} @_ZN1S1fEi(i32 noundef %[[ARG]])
int call_default_arg_1() { return S::f(); }

// An explicit argument suppresses the default, so nothing from the body is
// emitted at this call.
// CHECK-LABEL: define {{.*}} @_Z18call_default_arg_2v
// CHECK-NOT: store i32 8
// CHECK: ret
int call_default_arg_2() { return S::f(3); }

// A local class in a default argument's body: the enclosing-function type of
// the local class has to be manglable.
struct WithLocalClass {
  static int f(int v = do { struct L { int a; }; L l{9}; do_return l.a; }) {
    return v;
  }
};
// CHECK-LABEL: define {{.*}} @_Z21call_with_local_classv
// CHECK: %[[L:.*]] = alloca %struct.L
// CHECK: call void @llvm.memcpy{{.*}}(ptr align 4 %[[L]], ptr align 4 @__const._ZN14WithLocalClass14__do_expr_bodyEv.l
// CHECK: call {{.*}} @_ZN14WithLocalClass1fEi
int call_with_local_class() { return WithLocalClass::f(); }

// A default member initializer goes through a different marking path (the
// implicit default constructor), and must emit too.
struct D {
  int x = do { int j = 10; do_return j; };
};
// CHECK-LABEL: define {{.*}} @_Z13call_dmi_oncev
// CHECK: %[[DRES:.*]] = alloca i32
// CHECK: %[[DJ:.*]] = alloca i32
// CHECK: store i32 10, ptr %[[DJ]]
// CHECK: %[[DV:.*]] = load i32, ptr %[[DJ]]
// CHECK: store i32 %[[DV]], ptr %[[DRES]]
int call_dmi_once() { return D{}.x; }
