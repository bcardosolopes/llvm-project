// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -debug-info-kind=limited \
// RUN:   -emit-llvm %s -o - | FileCheck %s

// Lexical blocks must nest: a variable declared in an inner block is parented
// through the enclosing blocks (not flattened directly under the subprogram), so
// enclosing-scope variables (e.g. a loop counter) stay visible inside the body.

int g;
void f(int a) {
  for (int i = 0; i < a; ++i) {
    int s = i;
    g += s;
  }
}

// The loop counter `i` lives in the for-statement lexical block; the loop body
// block (holding `s`) is nested inside it, so `s`'s scope is that for block.
// CHECK-DAG: !DILocalVariable(name: "i",{{.*}} scope: ![[FOR:[0-9]+]]
// CHECK-DAG: !DILocalVariable(name: "s",{{.*}} scope: ![[BODY:[0-9]+]]
// CHECK-DAG: ![[BODY]] = distinct !DILexicalBlock(scope: ![[FOR]],
