// RUN: %clang_cc1 -triple arm-linux-gnueabihf -mfloat-abi hard -ffreestanding \
// RUN:   -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple arm-linux-gnueabihf -mfloat-abi hard -ffreestanding \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple arm-linux-gnueabihf -mfloat-abi soft -ffreestanding \
// RUN:   -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple arm-linux-gnueabihf -mfloat-abi soft -ffreestanding \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// In C++ an empty struct has size 1 and must occupy an argument slot, passed as
// an i8, per the ARM C++ ABI. ClangIR previously treated the empty record as
// ignorable and dropped the parameter entirely, producing an ABI-incompatible
// signature. It now passes the empty struct as a 1-byte i8 slot, matching
// classic CodeGen. (In pure C an empty struct is size 0 and is still dropped;
// that path is unaffected by this change.)

struct E {};

// CIR: define dso_local {{.*}}i32 @_Z1f1Ei(i8 %{{.*}}, i32 noundef %{{.*}})
// OGCG: define dso_local {{.*}}i32 @_Z1f1Ei(i8 %{{.*}}, i32 noundef %{{.*}})
int f(E e, int x) { return x; }

// CIR: define dso_local {{.*}}i32 @_Z1gi1E(i32 noundef %{{.*}}, i8 %{{.*}})
// OGCG: define dso_local {{.*}}i32 @_Z1gi1E(i32 noundef %{{.*}}, i8 %{{.*}})
int g(int x, E e) { return x; }
