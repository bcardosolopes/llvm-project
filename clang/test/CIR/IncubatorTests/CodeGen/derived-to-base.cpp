// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -mconstructor-aliases -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

typedef enum {
  RequestFailed = -2004,
} enumy;

typedef struct {
  const void* samples;
  int cound;
} buffy;

class C1 {
 public:
  virtual ~C1();
  C1(int i);

  struct IE {
    bool supported = false;
    unsigned version = 0;
  };

  struct IEs {
    IE chain;
  };

  static IEs availableIEs;
  class Layer {
   public:
    Layer(int d);
    virtual ~Layer() {}
  };

  virtual enumy SetStuff(enumy e, buffy b);
  virtual enumy Initialize() = 0;
};

class C2 : public C1 {
 public:
  C2(
    void* p,
    int i
  );

  ~C2() override;

  class Layer : public C1::Layer {
   public:
    Layer(int d, const C2* C1);
    virtual ~Layer();

   protected:
    const C2* m_C1;
  };

  virtual enumy SetStuff(enumy e, buffy b) override;
  virtual enumy Initialize() override;
};

class C3 : public C2 {
  struct Layer : public C2::Layer {
   public:
    Layer(int d, const C2* C1);
    void Initialize();
  };

  virtual enumy Initialize() override;
};

void C3::Layer::Initialize() {
  if (m_C1 == nullptr) {
    return;
  }
  if (m_C1->availableIEs.chain.supported) {
  }
}

// CHECK-DAG: !rec_C23A3ALayer = !cir.record<class "C2::Layer"
// CHECK-DAG: !rec_C33A3ALayer = !cir.record<struct "C3::Layer"
// CHECK-DAG: !rec_A = !cir.record<class "A"
// CHECK-DAG: !rec_A2Ebase = !cir.record<class "A.base" packed
// CHECK-DAG: !rec_B = !cir.record<class "B" {!rec_A2Ebase

// CHECK: cir.func {{.*}} @_ZN2C35Layer10InitializeEv

// CHECK:    %[[BASE:.*]] = cir.base_class_addr %{{.*}} : !cir.ptr<!rec_C33A3ALayer> nonnull [0] -> !cir.ptr<!rec_C23A3ALayer>
// CHECK:    %[[MEMBER:.*]] = cir.get_member %[[BASE]][1] {name = "m_C1"} : !cir.ptr<!rec_C23A3ALayer> -> !cir.ptr<!cir.ptr<!rec_C2>>
// CHECK:    %[[LOAD:.*]] = cir.load{{.*}} %[[MEMBER]] : !cir.ptr<!cir.ptr<!rec_C2>>, !cir.ptr<!rec_C2>
// CHECK:    %[[NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!rec_C2>
// CHECK:    %[[CMP:.*]] = cir.cmp(eq, %[[LOAD]], %[[NULL]]) : !cir.ptr<!rec_C2>, !cir.bool

enumy C3::Initialize() {
  return C2::Initialize();
}

// CHECK: cir.func {{.*}} @_ZN2C310InitializeEv(%arg0: !cir.ptr<!rec_C3>
// CHECK:     %0 = cir.alloca !cir.ptr<!rec_C3>, !cir.ptr<!cir.ptr<!rec_C3>>, ["this", init] {alignment = 8 : i64}

// CHECK:     cir.store %arg0, %0 : !cir.ptr<!rec_C3>, !cir.ptr<!cir.ptr<!rec_C3>>
// CHECK:     %2 = cir.load{{.*}} %0 : !cir.ptr<!cir.ptr<!rec_C3>>, !cir.ptr<!rec_C3>
// CHECK:     %3 = cir.base_class_addr %2 : !cir.ptr<!rec_C3> nonnull [0] -> !cir.ptr<!rec_C2>
// CHECK:     %4 = cir.call @_ZN2C210InitializeEv(%3) : (!cir.ptr<!rec_C2>) -> !s32i

void vcall(C1 &c1) {
  buffy b;
  enumy e;
  c1.SetStuff(e, b);
}

// CHECK: cir.func {{.*}} @_Z5vcallR2C1(%arg0: !cir.ptr<!rec_C1>
// CHECK:   %0 = cir.alloca !cir.ptr<!rec_C1>, !cir.ptr<!cir.ptr<!rec_C1>>, ["c1", init, const] {alignment = 8 : i64}
// CHECK:   %1 = cir.alloca !rec_buffy, !cir.ptr<!rec_buffy>, ["b"] {alignment = 8 : i64}
// CHECK:   %2 = cir.alloca !s32i, !cir.ptr<!s32i>, ["e"] {alignment = 4 : i64}
// CHECK:   %3 = cir.alloca !rec_buffy, !cir.ptr<!rec_buffy>, ["agg.tmp0"] {alignment = 8 : i64}
// CHECK:   cir.store %arg0, %0 : !cir.ptr<!rec_C1>, !cir.ptr<!cir.ptr<!rec_C1>>
// CHECK:   %4 = cir.load{{.*}} %0 : !cir.ptr<!cir.ptr<!rec_C1>>, !cir.ptr<!rec_C1>
// CHECK:   %5 = cir.load{{.*}} %2 : !cir.ptr<!s32i>, !s32i
// CHECK:   cir.copy %1 to %3 {{.*}}: !cir.ptr<!rec_buffy>
// CHECK:   %6 = cir.vtable.get_vptr %4 : !cir.ptr<!rec_C1> -> !cir.ptr<!cir.vptr>
// CHECK:   %7 = cir.load align(8) %6 : !cir.ptr<!cir.vptr>, !cir.vptr
// CHECK:   %8 = cir.vtable.get_virtual_fn_addr %7[2] : !cir.vptr -> !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_C1>, !s32i, !rec_buffy) -> !s32i>>>
// CHECK:   %9 = cir.load align(8) %8 : !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_C1>, !s32i, !rec_buffy) -> !s32i>>>, !cir.ptr<!cir.func<(!cir.ptr<!rec_C1>, !s32i, !rec_buffy) -> !s32i>>
// CHECK:   %10 = cir.cast bitcast %3 : !cir.ptr<!rec_buffy> -> !cir.ptr<!rec_anon_struct{{.*}}>
// CHECK:   %11 = cir.get_member %10[0] {name = ""} : !cir.ptr<!rec_anon_struct{{.*}}> -> !cir.ptr<!cir.ptr<!void>>
// CHECK:   %12 = cir.load align(8) %11 : !cir.ptr<!cir.ptr<!void>>, !cir.ptr<!void>
// CHECK:   %13 = cir.get_member %10[1] {name = ""} : !cir.ptr<!rec_anon_struct{{.*}}> -> !cir.ptr<!s32i>
// CHECK:   %14 = cir.load align(8) %13 : !cir.ptr<!s32i>, !s32i
// CHECK:   %15 = cir.cast bitcast %9 : !cir.ptr<!cir.func<(!cir.ptr<!rec_C1>, !s32i, !rec_buffy) -> !s32i>> -> !cir.ptr<!cir.func<(!cir.ptr<!rec_C1>, !s32i, !cir.ptr<!void>, !s32i) -> !s32i>>
// CHECK:   %16 = cir.call %15(%4, %5, %12, %14) {cir.call_arg_attrs = [{cir.align = 8 : i64, cir.dereferenceable = 8 : i64, cir.nonnull, cir.noundef}, {cir.noundef}, {}, {}]{{.*}} : (!cir.ptr<!cir.func<(!cir.ptr<!rec_C1>, !s32i, !cir.ptr<!void>, !s32i) -> !s32i>>, !cir.ptr<!rec_C1>, !s32i, !cir.ptr<!void>, !s32i) -> !s32i
// CHECK:   cir.return
// CHECK: }

class A {
public:
  int a;
  virtual void foo() {a++;}
};

class B : public A {
public:
  int b;
  void foo ()  { static_cast<A>(*this).foo();}
};

// CHECK: cir.func {{.*}} @_ZN1B3fooEv(%arg0: !cir.ptr<!rec_B>
// CHECK:   %0 = cir.alloca !cir.ptr<!rec_B>, !cir.ptr<!cir.ptr<!rec_B>>, ["this", init] {alignment = 8 : i64}
// CHECK:   cir.store %arg0, %0 : !cir.ptr<!rec_B>, !cir.ptr<!cir.ptr<!rec_B>>
// CHECK:   %1 = cir.load{{.*}} deref %0 : !cir.ptr<!cir.ptr<!rec_B>>, !cir.ptr<!rec_B>
// CHECK:   cir.scope {
// CHECK:     %2 = cir.alloca !rec_A, !cir.ptr<!rec_A>, ["ref.tmp0"] {alignment = 8 : i64}
// CHECK:     %3 = cir.base_class_addr %1 : !cir.ptr<!rec_B> nonnull [0] -> !cir.ptr<!rec_A>

// Call @A::A(A const&). A has no virtual base, so its complete-object
// constructor is folded into the base one and the call targets C2.
// CHECK:     cir.call @_ZN1AC2ERKS_(%2, %3) nothrow : (!cir.ptr<!rec_A>, !cir.ptr<!rec_A>) -> ()

// Call @A::foo()
// CHECK:     cir.call @_ZN1A3fooEv(%2) : (!cir.ptr<!rec_A>) -> ()
// CHECK:   }
// CHECK:   cir.return
// CHECK: }

void t() {
  B b;
  b.foo();
}

struct C : public A {
  int& ref;
  C(int& x) : ref(x) {}
};

// CHECK: cir.func {{.*}} @_Z8test_refv()
// CHECK: %{{.*}} = cir.get_member %1[1] {name = "ref"} : !cir.ptr<!rec_C> -> !cir.ptr<!cir.ptr<!s32i>>
int test_ref() {
  int x = 42;
  C c(x);
  return c.ref;
}

// Multiple base classes, to test non-zero offsets
struct Base1 { int a; };
struct Base2 { int b; };
struct Derived : Base1, Base2 { int c; };
void test_multi_base() {
  Derived d;

  Base2& bref = d; // no null check needed
  // CHECK: %6 = cir.base_class_addr %0 : !cir.ptr<!rec_Derived> nonnull [4] -> !cir.ptr<!rec_Base2>

  Base2* bptr = &d; // has null pointer check
  // CHECK: %7 = cir.base_class_addr %0 : !cir.ptr<!rec_Derived> [4] -> !cir.ptr<!rec_Base2>

  int a = d.a;
  // CHECK: %{{.*}} = cir.base_class_addr %0 : !cir.ptr<!rec_Derived> nonnull [0] -> !cir.ptr<!rec_Base1>
  // CHECK: %{{.*}} = cir.get_member %{{.*}}[0] {name = "a"} : !cir.ptr<!rec_Base1> -> !cir.ptr<!s32i>

  int b = d.b;
  // CHECK: %{{.*}} = cir.base_class_addr %0 : !cir.ptr<!rec_Derived> nonnull [4] -> !cir.ptr<!rec_Base2>
  // CHECK: %{{.*}} = cir.get_member %{{.*}}[0] {name = "b"} : !cir.ptr<!rec_Base2> -> !cir.ptr<!s32i>

  int c = d.c;
  // CHECK: %{{.*}} = cir.get_member %0[2] {name = "c"} : !cir.ptr<!rec_Derived> -> !cir.ptr<!s32i>
}
