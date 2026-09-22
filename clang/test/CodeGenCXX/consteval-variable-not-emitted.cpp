// RUN: %clang_cc1 -std=c++2d -freflection -fconsteval-operations -emit-llvm -o - %s | FileCheck %s
// Copyright 2026 Jump Trading, LLC

// A consteval variable -- written, or a constexpr variable silently upgraded
// because its value holds consteval-only content -- exists only during
// translation and is never emitted; its uses are constant-folded and every
// runtime escape is diagnosed by Sema. (Emitting one used to crash code
// generation when its value persisted an allocation, e.g. the compile-time
// range of a 'template for (consteval ...)' over a vector<info>; that
// end-to-end case is covered by the libc++ test
// expansion-consteval-range.pass.cpp, which needs the real vector.)

using info = decltype(^^int);

struct Node {
  info r;
  int v;
};

consteval int peek(const Node &n) { return (n.r == ^^int) ? n.v : -1; }

// A local consteval variable of consteval-only type: no alloca, no store;
// only the constant-folded use remains.
int local() {
  consteval Node n{^^int, 41};
  return peek(n) + 1;
}
// CHECK-LABEL: define {{.*}} @_Z5localv
// CHECK-NOT: alloca
// CHECK: ret i32 42

// Same at namespace scope: no global is emitted for the variable.
consteval Node g{^^int, 7};
int global() { return peek(g); }
// CHECK-LABEL: define {{.*}} @_Z6globalv
// CHECK: ret i32 7

// CHECK-NOT: @g ={{.*}}global
// CHECK-NOT: __nta_
