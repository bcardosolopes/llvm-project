// RUN: %clang_cc1 -std=c++17 -triple aarch64-none-linux-android21 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that __cxxabiv1::__fundamental_type_info triggers emission of RTTI
// descriptors for all fundamental types (void, int, float, etc.) plus their
// pointer and const-pointer variants.
//
//

namespace __cxxabiv1 {
class __shim_type_info {
public:
  virtual ~__shim_type_info();
};

class __fundamental_type_info : public __shim_type_info {
public:
  virtual ~__fundamental_type_info();
};

__fundamental_type_info::~__fundamental_type_info() {}
}

// Fundamental type: void (and its pointer/const-pointer variants)
// CHECK: cir.global {{.*}} @_ZTIv =
// CHECK: cir.global {{.*}} @_ZTIPv =
// CHECK: cir.global {{.*}} @_ZTIPKv =

// Fundamental type: int
// CHECK: cir.global {{.*}} @_ZTIi =
// CHECK: cir.global {{.*}} @_ZTIPi =
// CHECK: cir.global {{.*}} @_ZTIPKi =

// Fundamental type: float
// CHECK: cir.global {{.*}} @_ZTIf =
// CHECK: cir.global {{.*}} @_ZTIPf =
// CHECK: cir.global {{.*}} @_ZTIPKf =

// Fundamental type: double
// CHECK: cir.global {{.*}} @_ZTId =
// CHECK: cir.global {{.*}} @_ZTIPd =
// CHECK: cir.global {{.*}} @_ZTIPKd =
