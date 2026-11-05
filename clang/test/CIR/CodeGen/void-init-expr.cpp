// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that void{} and void() expressions compile without crashing.
// GH116440: void{} is a valid C++ prvalue of type void.

namespace GH116440 {
void f() {
  void{};
  void();
}
}

// CHECK: cir.func {{.*}} @_ZN8GH1164401fEv()
// CHECK-NEXT:   cir.return
