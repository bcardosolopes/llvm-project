// RUN: %clang_cc1 -std=c++14 -triple x86_64-linux-gnu -disable-llvm-passes \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that variable template init functions appear in dependency order
// in @llvm.global_ctors (base cases before dependent cases)..

template<int N> int Fib = Fib<N-2> + Fib<N-1>;
template<> int Fib<0> = 0;
template<> int Fib<1> = 1;
int f = Fib<5>;

// CHECK: @llvm.global_ctors = appending global
// Fib<2> before Fib<3> before Fib<4> before Fib<5>:
// CHECK-SAME: ptr @_Z3FibILi2EE
// CHECK-SAME: ptr @_Z3FibILi3EE
// CHECK-SAME: ptr @_Z3FibILi4EE
// CHECK-SAME: ptr @_Z3FibILi5EE
