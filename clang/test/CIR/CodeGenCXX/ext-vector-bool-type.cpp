// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
//
// ext_vector_type with bool now works for load/store via packed integer
// storage (was previously NYI crash).

typedef bool bvec4 __attribute__((ext_vector_type(4)));

bvec4 test(bvec4 v) {
  return v;
}

// CHECK: cir.func {{.*}} @_Z4testDv4_b
// CHECK:   cir.cast bitcast {{.*}} !u8i -> !cir.vector<8 x !cir.bool>
// CHECK:   cir.vec.shuffle

// LLVM: define {{.*}} @_Z4testDv4_b
// LLVM:   load i8
// LLVM:   bitcast i8 %{{.*}} to <8 x i1>
// LLVM:   shufflevector <8 x i1> %{{.*}}, <8 x i1> poison, <4 x i32> <i32 0, i32 1, i32 2, i32 3>
