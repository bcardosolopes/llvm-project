// The CIR inliner does not copy a recursive cycle into itself: around a cycle
// only the size rule applies, not always_inline (or the only-use rule), so a,
// b and c each stay a function calling the next. Before, each round inlined
// one more level of the cycle -- generated encoders for recursive types grew
// to 12 M instructions in one real library.
//
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -c %s -o %t.o
// RUN: cir-link %t.o --inline --emit-llvm --emit-llvm-text -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// CHECK-DAG: define internal i32 @a(
// CHECK-DAG: define internal i32 @b(
// CHECK-DAG: define internal i32 @c(
// CHECK-DAG: call i32 @c(
// CHECK-DAG: call i32 @b(

__attribute__((always_inline)) static inline int a(int);
__attribute__((always_inline)) static inline int b(int x) {
  return x > 3 ? a(x - 1) + x * 5 : x;
}
__attribute__((always_inline)) static inline int c(int x) {
  return x > 2 ? b(x - 1) * 2 - x : 1;
}
__attribute__((always_inline)) static inline int a(int x) {
  return x > 1 ? c(x - 1) - 1 : 2;
}
int entry(int x) { return a(x) + a(x + 1); }
