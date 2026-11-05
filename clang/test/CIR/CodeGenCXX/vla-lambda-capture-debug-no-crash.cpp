// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++11 -fclangir -emit-llvm \
// RUN:   -debug-info-kind=constructor %s -o /dev/null
//
// Compiling a lambda that captures a VLA with
// debug info crashed because getLambdaCaptureDebugName called getCapturedVar
// on a VLA capture, which asserts capturesVariable().

typedef __INTPTR_TYPE__ intptr_t;

void g(intptr_t n) {
  intptr_t buffer[n];
  [&buffer, &n]() {
    __typeof(buffer) x;
  }();
}
