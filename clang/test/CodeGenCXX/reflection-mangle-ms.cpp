// RUN: %clang_cc1 -std=c++26 -freflection -triple x86_64-pc-windows-msvc \
// RUN:   -emit-llvm -o - %s -verify

int main() {
  (void)(^^int); // expected-error {{expressions of consteval-only type are only allowed in constant-evaluated contexts}}
  return 0;
}
