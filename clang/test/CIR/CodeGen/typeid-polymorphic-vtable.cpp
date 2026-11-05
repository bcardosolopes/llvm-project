// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

namespace std {
  class type_info {
  public:
    virtual ~type_info();
    const char *name() const;
  };
}

struct Poly {
  virtual ~Poly();
};

struct Derived : Poly {};

// Test: typeid on a polymorphic reference — runtime vtable walk, no null check.
const std::type_info& get_typeid_ref(Poly &p) {
  return typeid(p);
}

// CHECK-LABEL: @_Z14get_typeid_refR4Poly
// CHECK:         cir.vtable.get_vptr
// CHECK:         cir.const #cir.int<-1> : !s64i
// CHECK:         cir.ptr_stride
// CHECK:         cir.load
// CHECK:         cir.cast bitcast
// CHECK-NOT:     @__cxa_bad_typeid
// CHECK:         cir.return

// Test: typeid on pointer dereference — has null check with __cxa_bad_typeid.
const std::type_info& get_typeid_ptr(Poly *p) {
  return typeid(*p);
}

// CHECK-LABEL: @_Z14get_typeid_ptrP4Poly
// CHECK:         cir.const #cir.ptr<null>
// CHECK:         cir.cmp(eq,
// CHECK:         cir.if
// CHECK:           cir.call @__cxa_bad_typeid
// CHECK:           cir.unreachable
// CHECK:         }
// CHECK:         cir.vtable.get_vptr
// CHECK:         cir.const #cir.int<-1> : !s64i
// CHECK:         cir.ptr_stride
// CHECK:         cir.return
