// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm \
// RUN:   -std=c++20 -w -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -emit-llvm \
// RUN:   -std=c++20 -w -o - %s | FileCheck %s --check-prefix=OGCG

// Regression test: large zero-initialized arrays must use llvm.memset
// instead of store zeroinitializer. A store of [N x i8] with N > 65535
// overflows SelectionDAG's SDNode::NumValues (unsigned short).

template<int N>
struct Buffer {
  unsigned char data[N];
};

struct Wrapper {
  Buffer<100000> buf;
  int tag;
  Wrapper() : buf{}, tag(0) {}
};

void test() {
  Wrapper w;
}

// The constructor should use memset for the large zero-init, not
// store zeroinitializer.
// CHECK-LABEL: @_ZN7WrapperC
// CHECK: memset
// CHECK-NOT: store [100000 x i8] zeroinitializer

// OGCG-LABEL: @_ZN7WrapperC
// OGCG: memset
// OGCG-NOT: store [100000 x i8] zeroinitializer
