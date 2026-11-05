// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s \
// RUN:   -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -std=c++17 -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// A musttail return of a value that the ABI returns indirectly (via sret) must
// forward the function's own sret pointer to the callee and emit a bare return:
// a musttail call must be immediately followed by the return with no
// intervening store. On i386 a _BitInt(100) is returned via sret. Previously the
// musttail-return epilog for an sret _BitInt either aborted (no undef handler)
// or stored the result after the musttail call (broken module).

signed _BitInt(100) callee(float, double);

signed _BitInt(100) test_musttail(float a, double b) {
  [[clang::musttail]] return callee(a, b);
}

// The musttail call forwards the function's sret argument and is immediately
// followed by a bare return; no store sits between them.
// LLVM-LABEL: @_Z13test_musttailfd
// LLVM: musttail call void @{{.*}}callee{{.*}}(ptr {{.*}}sret
// LLVM-NEXT: ret void

// OGCG-LABEL: @_Z13test_musttailfd
// OGCG: musttail call void @{{.*}}callee
// OGCG-NEXT: ret void

// A non-musttail sret _BitInt return preceding the musttail one must still work
// (the musttail handling is gated and does not disturb the normal path).
signed _BitInt(100) test_plain(float a, double b) {
  return callee(a, b);
}

// LLVM-LABEL: @_Z10test_plainfd
// OGCG-LABEL: @_Z10test_plainfd
