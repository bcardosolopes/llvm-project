// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcxx-exceptions -fexceptions %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -fcxx-exceptions -fexceptions %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj -fcxx-exceptions -fexceptions %s -o %t.o
//
// Runtime typeid(expr) on polymorphic objects — load type_info
// from vtable at offset -1.

namespace std {
  class type_info {
  public:
    virtual ~type_info();
    bool operator==(const type_info&) const;
  };
}

struct Base { virtual ~Base() {} };
struct Derived : Base {};

// Runtime typeid on reference (loads vtable, indexes -1)
bool test_ref(Base& b) {
    return typeid(b) == typeid(Derived);
}

// Runtime typeid on pointer dereference (with null check)
bool test_deref(Base* p) {
    return typeid(*p) == typeid(Derived);
}

// CIR: cir.if
// CIR: cir.call @__cxa_bad_typeid

// LLVM: @_ZTI7Derived
// LLVM: @_ZTI4Base

// Verify direct-CIR matches OGCG by keeping noreturn on the call site only.
// LLVM: declare void @__cxa_bad_typeid()
// LLVM: call void @__cxa_bad_typeid() [[BAD_TYPEID_ATTRS:#[0-9]+]]
// LLVM: attributes [[BAD_TYPEID_ATTRS]] = { noreturn }
