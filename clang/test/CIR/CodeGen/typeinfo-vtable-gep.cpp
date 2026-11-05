// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-call-conv-lowering -emit-llvm -std=c++23 -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Typeinfo globals contain GlobalViewAttr references to vtables with
// indices. Vtable types are struct { [N x ptr] } in CIR, and the indices
// refer to array elements, not struct members. Verify this doesn't crash
// and produces correct typeinfo GEPs.

class Base {
public:
  virtual ~Base();
  virtual void f();
};
Base::~Base() {}
void Base::f() {}

class Derived : public Base {
public:
  ~Derived() override;
  void f() override;
};
Derived::~Derived() {}
void Derived::f() {}

// CHECK-DAG: @_ZTV4Base = {{.*}}constant
// CHECK-DAG: @_ZTV7Derived = {{.*}}constant
// CHECK-DAG: @_ZTI4Base = constant { ptr, ptr } { ptr getelementptr inbounds (i8, ptr @_ZTVN10__cxxabiv117__class_type_infoE, i64 16)
// CHECK-DAG: @_ZTI7Derived = constant { ptr, ptr, ptr } { ptr getelementptr inbounds (i8, ptr @_ZTVN10__cxxabiv120__si_class_type_infoE, i64 16)
