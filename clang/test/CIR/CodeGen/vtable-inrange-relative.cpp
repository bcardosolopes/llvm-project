// RUN: %clang_cc1 -triple aarch64-linux-gnu -fexperimental-relative-c++-abi-vtables \
// RUN:   -fvisibility=hidden -fclangir -emit-llvm -std=c++17 -o - %s \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fexperimental-relative-c++-abi-vtables \
// RUN:   -fvisibility=hidden -emit-llvm -std=c++17 -o - %s \
// RUN:   | FileCheck %s --check-prefix=OGCG

// Verify that inrange bounds use 4-byte component size for relative vtables
// (i32 entries) instead of 8-byte pointer size.

struct Mother {
  virtual void MotherMethod();
  virtual void MotherMethod2();
};
struct Father {
  virtual void FatherMethod();
};
struct Child : Mother, Father {
  void MotherMethod() override;
};

void test() { Child c; }

// Mother sub-vtable: [4 x i32], address point at index 2
//   inrange: -(2*4)=-8, (4-2)*4=8
// Father sub-vtable: [3 x i32], address point at index 2
//   inrange: -(2*4)=-8, (3-2)*4=4

// CIR: getelementptr inbounds inrange(-8, 8) ({{.*}}, ptr @_ZTV5Child, i32 0, i32 0,
// CIR: getelementptr inbounds inrange(-8, 4) ({{.*}}, ptr @_ZTV5Child, i32 0, i32 1,

// OGCG: getelementptr inbounds inrange(-8, 8) ({{.*}}, ptr @_ZTV5Child, i32 0, i32 0,
// OGCG: getelementptr inbounds inrange(-8, 4) ({{.*}}, ptr @_ZTV5Child, i32 0, i32 1,
