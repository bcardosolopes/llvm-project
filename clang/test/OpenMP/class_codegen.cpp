// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -x c++ -std=c++11 -emit-llvm %s -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -fopenmp -fclangir -x c++ -std=c++11 -emit-llvm %s -o - | FileCheck %s %}

class C {
public:
  int a;
  double *b;
};

// CHECK-LABEL: define {{.*}} i32 @_Z1f1C
int f(C c) { return c.a; }
