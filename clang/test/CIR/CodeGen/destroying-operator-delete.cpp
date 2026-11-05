// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test that destroying operator delete is correctly handled by CIR codegen.
// The deleting destructor (D0) should call the destroying delete operator
// and then properly return through cleanup scopes.

namespace std {
  struct destroying_delete_t {
    explicit destroying_delete_t() = default;
  };
  inline constexpr destroying_delete_t destroying_delete{};
}

struct Foo {
  int x;
  virtual ~Foo();
  void operator delete(Foo *p, std::destroying_delete_t);
};

Foo::~Foo() {}

void test(Foo *p) {
  delete p;
}

// The deleting destructor (D0) should call the destroying operator delete
// and return, without calling the complete destructor.
// The destroying_delete_t tag alloca should be elided.
// CIR-LABEL: cir.func {{.*}} @_ZN3FooD0Ev
// CIR-NOT:     cir.alloca !rec_std3A3Adestroying_delete_t
// CIR:         cir.call @_ZN3FoodlEPS_St19destroying_delete_t(
// CIR-NOT:     cir.call @_ZN3FooD1Ev
// CIR-NOT:     cir.call @_ZN3FooD2Ev
// CIR:         cir.return
// CIR:       }

// CIR-LABEL: cir.func {{.*}} @_Z4testP3Foo

// LLVM-LABEL: define {{.*}} @_ZN3FooD0Ev
// LLVM-NOT: alloca %"struct.std::destroying_delete_t"
// LLVM: call void @_ZN3FoodlEPS_St19destroying_delete_t
// LLVM-NOT: call {{.*}}@_ZN3FooD1Ev
// LLVM-NOT: call {{.*}}@_ZN3FooD2Ev
// LLVM: ret void

// LLVM-LABEL: define {{.*}} @_Z4testP3Foo
// LLVM: icmp {{ne|eq}} ptr %{{.*}}, null

// Test with a derived class to ensure cleanup scopes are properly handled.
struct Base {
  int b;
  virtual ~Base();
};

struct Derived : Base {
  int d;
  ~Derived() override;
  void operator delete(Derived *p, std::destroying_delete_t);
};

Derived::~Derived() {}

void test_derived(Derived *p) {
  delete p;
}

// The D0 deleting destructor for a derived class with destroying operator
// delete should call the destroying delete and return without invoking the
// complete destructor or base destructor.
// CIR-LABEL: cir.func {{.*}} @_ZN7DerivedD0Ev
// CIR-NOT:     cir.alloca !rec_std3A3Adestroying_delete_t
// CIR:         cir.call @_ZN7DeriveddlEPS_St19destroying_delete_t(
// CIR-NOT:     cir.call @_ZN7DerivedD1Ev
// CIR-NOT:     cir.call @_ZN7DerivedD2Ev
// CIR-NOT:     cir.call @_ZN4BaseD2Ev
// CIR:         cir.return
// CIR:       }

// LLVM-LABEL: define {{.*}} @_ZN7DerivedD0Ev
// LLVM-NOT: alloca %"struct.std::destroying_delete_t"
// LLVM: call void @_ZN7DeriveddlEPS_St19destroying_delete_t
// LLVM-NOT: call {{.*}}@_ZN7DerivedD1Ev
// LLVM-NOT: call {{.*}}@_ZN7DerivedD2Ev
// LLVM-NOT: call {{.*}}@_ZN4BaseD2Ev
// LLVM: ret void
