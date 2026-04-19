// RUN: %clang_cc1 -std=c++26 -freflection -triple x86_64-unknown-linux-gnu \
// RUN:   -emit-llvm -o - %s -verify

int main() {
  (void)(^^int); // expected-error {{expressions involving consteval-only values are only allowed in constant-evaluated contexts}}
  return 0;
}
