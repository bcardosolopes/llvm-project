// A function one TU defines and another declares with a signature no bitcast
// reconciles -- OpenBLAS's `double sdot_` called as `float sdot_` by PyTorch,
// a void function called as returning int, and a three-parameter function
// called with two arguments through a stale prototype, as generated code in
// one real library calls a five-parameter function with four. The call keeps
// the type its caller was compiled with and reaches the definition through it,
// as a classic link does, instead of leaving a module that does not verify.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O0 -c %t/def.c -o %t/def.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O0 -c %t/call.c -o %t/call.o
// RUN: cir-link %t/def.o %t/call.o --emit-llvm --emit-llvm-text \
// RUN:     -o %t/def-first.ll
// RUN: FileCheck --input-file=%t/def-first.ll %s
// RUN: cir-link %t/call.o %t/def.o --emit-llvm --emit-llvm-text \
// RUN:     -o %t/call-first.ll
// RUN: FileCheck --input-file=%t/call-first.ll %s
//
// CHECK-DAG: define {{.*}}double @sdot_(
// CHECK-DAG: define {{.*}}void @reset(
// CHECK-DAG: call float @sdot_(ptr
// CHECK-DAG: call i32 @reset()
// CHECK-DAG: define {{.*}}i32 @sum3(
// CHECK-DAG: call i32 @sum3(i32 {{.*}}, i32

//--- def.c
double sdot_(const int *n, const float *x) {
  double sum = 0;
  for (int i = 0; i < *n; ++i)
    sum += x[i];
  return sum;
}
void reset(void) {}
int sum3(int a, int b, int c) { return a + b + c; }

//--- call.c
float sdot_(const int *n, const float *x);
int reset(void);
int sum3(int a, int b);
float dot(const float *x, int n) { return sdot_(&n, x) + reset(); }
int pair(int a, int b) { return sum3(a, b); }
