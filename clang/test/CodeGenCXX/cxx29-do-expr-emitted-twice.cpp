// RUN: %clang_cc1 -std=c++2d -emit-llvm -triple x86_64-linux-gnu -O0 -disable-O0-optnone -o - %s | FileCheck %s

// Emitting the same do-expression AST twice in one function.
//
// Almost every expression that can declare a variable also delimits the
// function that variable lives in, so LocalDeclMap -- which is per-function
// and keyed by VarDecl -- never sees the same declaration twice. A
// do-expression breaks that: a default member initializer and a default
// argument are each one AST that CodeGen emits again at every use, so two
// uses in the same function reach the same VarDecl twice and the second one
// hits
//
//   Assertion `!LocalDeclMap.count(VD) && "Decl already exists in
//   LocalDeclMap!"'
//
// The body's declarations are unreachable once the body is over -- nothing
// outside it can name them -- so EmitDoExpr takes them back out of
// LocalDeclMap on the way out, and the second emission allocates its own
// storage. The captures below name the variable and whatever uniquing suffix
// CodeGen gave the second copy.

struct D {
  int x = do { int j = 10; do_return j; };
};

// Two separate allocas for the one `j`, and two separate stores.
// CHECK-LABEL: define {{.*}} @_Z12dmi_two_usesv
// CHECK: %[[J1:j[0-9]*]] = alloca i32
// CHECK: %[[J2:j[0-9]*]] = alloca i32
// CHECK: store i32 10, ptr %[[J1]]
// CHECK: store i32 10, ptr %[[J2]]
int dmi_two_uses() { return D{}.x + D{}.x; }

// A default argument is the other shared-AST shape.
int f(int v = do { int j = 20; do_return j; }) { return v; }

// CHECK-LABEL: define {{.*}} @_Z21default_arg_two_callsv
// CHECK: %[[K1:j[0-9]*]] = alloca i32
// CHECK: %[[K2:j[0-9]*]] = alloca i32
// CHECK: store i32 20, ptr %[[K1]]
// CHECK: store i32 20, ptr %[[K2]]
int default_arg_two_calls() { return f() + f(); }

// An init-capture is declared before the body is entered, so it has to be
// tracked from the same point.
struct C {
  int x = do [k = 30] { do_return k; };
};

// CHECK-LABEL: define {{.*}} @_Z21init_capture_two_usesv
// CHECK: %[[C1:k[0-9]*]] = alloca i32
// CHECK: %[[C2:k[0-9]*]] = alloca i32
// CHECK: store i32 30, ptr %[[C1]]
// CHECK: store i32 30, ptr %[[C2]]
int init_capture_two_uses() { return C{}.x + C{}.x; }

// A local class declared in the body: two objects, one type, one constant.
struct L {
  int x = do { struct Inner { int a; }; Inner i{40}; do_return i.a; };
};

// CHECK-LABEL: define {{.*}} @_Z20local_class_two_usesv
// CHECK: %[[I1:i[0-9]*]] = alloca %struct.Inner
// CHECK: %[[I2:i[0-9]*]] = alloca %struct.Inner
// CHECK: call void @llvm.memcpy{{.*}}(ptr align 4 %[[I1]], ptr align 4 @__const._ZN1L14__do_expr_bodyEv.i
// CHECK: call void @llvm.memcpy{{.*}}(ptr align 4 %[[I2]], ptr align 4 @__const._ZN1L14__do_expr_bodyEv.i
int local_class_two_uses() { return L{}.x + L{}.x; }

// A static local is one object no matter how many times the body is emitted:
// forgetting the mapping must not make a second copy of it.
struct SL {
  int x = do { static int counter = 50; do_return ++counter; };
};

// CHECK-LABEL: define {{.*}} @_Z21static_local_two_usesv
// CHECK: load i32, ptr @_ZZN2SL14__do_expr_bodyEvE7counter
// CHECK: store i32 %{{.*}}, ptr @_ZZN2SL14__do_expr_bodyEvE7counter
// CHECK: load i32, ptr @_ZZN2SL14__do_expr_bodyEvE7counter
// CHECK: store i32 %{{.*}}, ptr @_ZZN2SL14__do_expr_bodyEvE7counter
int static_local_two_uses() { return SL{}.x + SL{}.x; }

// A body local with a non-trivial destructor is constructed and destroyed
// once per emission, each on its own object.
struct Guard {
  int v;
  Guard(int v);
  ~Guard();
  int get() const;
};

struct G {
  int x = do { Guard g(60); do_return g.get(); };
};

// CHECK-LABEL: define {{.*}} @_Z14guard_two_usesv
// CHECK: %[[G1:g[0-9]*]] = alloca %struct.Guard
// CHECK: %[[G2:g[0-9]*]] = alloca %struct.Guard
// CHECK: call void @_ZN5GuardC1Ei(ptr {{.*}} %[[G1]], i32 noundef 60)
// CHECK: call void @_ZN5GuardD1Ev(ptr {{.*}} %[[G1]])
// CHECK: call void @_ZN5GuardC1Ei(ptr {{.*}} %[[G2]], i32 noundef 60)
// CHECK: call void @_ZN5GuardD1Ev(ptr {{.*}} %[[G2]])
int guard_two_uses() { return G{}.x + G{}.x; }

// The control: the same AST in two *different* functions was always fine,
// because LocalDeclMap is per-function. It still is -- neither one picks up a
// uniquing suffix.
// CHECK-LABEL: define {{.*}} @_Z12one_use_herev
// CHECK: %j = alloca i32
// CHECK: store i32 10, ptr %j
int one_use_here() { return D{}.x; }

// CHECK-LABEL: define {{.*}} @_Z13one_use_therev
// CHECK: %j = alloca i32
// CHECK: store i32 10, ptr %j
int one_use_there() { return D{}.x; }
