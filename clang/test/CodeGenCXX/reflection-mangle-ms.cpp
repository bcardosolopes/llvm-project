// RUN: %clang_cc1 -std=c++26 -freflection -fconsteval-operations -triple x86_64-pc-windows-msvc \
// RUN:   -emit-llvm -o - %s | FileCheck %s

// Reflections persist to runtime as a stateless 1-byte value, and the
// meta::info type participates in name mangling (MS type code "$$M").

using info = decltype(^^int);

// CHECK-LABEL: define{{.*}} i8 @"?make@@YA$$M$$M@Z"(i8 noundef %r)
info make(info r) { return r; }

// CHECK-LABEL: define{{.*}} i32 @main
int main() {
  // CHECK: store i8 0
  info r = ^^int;
  (void)(^^int); // ok: stateless no-op at runtime
  return 0;
}
