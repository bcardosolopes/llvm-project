// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 %s -o %t.ll
// Consteval default constructor with inheritance previously produced
// cir.store type mismatch. Now fixed.
//
// From clang/test/CodeGenCXX/cxx20-consteval-crash.cpp (GH60166).

struct Base {
  void* one = nullptr;
  void* two = nullptr;
};
struct Derived : Base {
  void* three = nullptr;
  consteval Derived() = default;
};
void method() {
  (void)Derived();
}
