// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fopenmp-targets=x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

struct Temp {
  int x;
  Temp();
  ~Temp();
};

struct Box {
  int x;
  Box();
  Box(int value);
  Box(const Box &other, Temp t = Temp());
  ~Box();
};

Temp::Temp() : x(7) {}
Temp::~Temp() {}
Box::Box() : x(0) {}
Box::Box(int value) : x(value) {}
Box::Box(const Box &other, Temp t) : x(other.x + t.x) {}
Box::~Box() {}

void firstprivate_nontrivial_default_arg(int *p) {
  Box b;
#pragma omp target teams distribute firstprivate(b) map(tofrom : p[0:2])
  for (int i = 0; i < 2; ++i)
    p[i] += b.x;
}

void firstprivate_nontrivial_array_default_arg(int *p) {
  Box b[] = {1, 2};
#pragma omp target teams distribute firstprivate(b) map(tofrom : p[0:2])
  for (int i = 0; i < 2; ++i)
    p[i] += b[i].x;
}

// OGCG-LABEL: @_Z35firstprivate_nontrivial_default_argPi(
// OGCG: call i32 @__tgt_target_kernel(

// OGCG-LABEL: @_Z41firstprivate_nontrivial_array_default_argPi(
// OGCG: call i32 @__tgt_target_kernel(
