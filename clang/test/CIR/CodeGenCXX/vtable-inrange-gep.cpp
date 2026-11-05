// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++14 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++14 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// VTable GEPs in global initializers must include the inrange
// annotation for devirtualization optimizations.

namespace test {
struct HasVTable {
  virtual void f();
};
inline HasVTable useStaticLocal() {
  static HasVTable obj;
  return obj;
}
void trigger() { useStaticLocal(); }
}

// The vtable pointer in the global initializer should use struct-indexed
// GEP with inrange bounds, not a raw byte-offset GEP.

// CHECK: @_ZZN4test14useStaticLocalEvE3obj = linkonce_odr global
// CHECK-SAME: getelementptr inbounds inrange(-16, 8) ({ [3 x ptr] }, ptr @_ZTVN4test9HasVTableE, i32 0, i32 0, i32 2)

// OGCG: @_ZZN4test14useStaticLocalEvE3obj = linkonce_odr global
// OGCG-SAME: getelementptr inbounds inrange(-16, 8) ({ [3 x ptr] }, ptr @_ZTVN4test9HasVTableE, i32 0, i32 0, i32 2)
