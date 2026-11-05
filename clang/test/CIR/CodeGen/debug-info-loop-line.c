// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -debug-info-kind=limited \
// RUN:   -gno-column-info -emit-llvm %s -o - | FileCheck %s

// A loop's entry and back-edge branches must be attributed to the loop
// statement's line, not DILocation line 0. A fused begin..end range location
// would merge the differing lines to 0; using a point location (the loop
// keyword) avoids that, matching classic CodeGen.

int g;
void f(int n) {
  for (int i = 0; i < n; ++i)
    g += i;
}

// The loop entry and back-edge branches carry the `for` line (11), not line 0.
// CHECK: br label %{{[0-9]+}}, !dbg ![[FOR:[0-9]+]]
// CHECK: br label %{{[0-9]+}}, !dbg ![[FOR]], !llvm.loop
// CHECK: ![[FOR]] = !DILocation(line: 11, scope: !{{[0-9]+}})
