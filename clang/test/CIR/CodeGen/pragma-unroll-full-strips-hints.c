// RUN: %clang_cc1 -O1 -disable-llvm-passes -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// When unroll(full) is specified alongside vectorize/interleave hints,
// OGCG strips the latter since full unrolling takes precedence.
// Verify CIR matches this behavior.

void test_unroll_full_strips_hints(int *a) {
#pragma clang loop unroll(full) vectorize_width(4) interleave_count(2) distribute(enable)
  for (int i = 0; i < 8; ++i)
    a[i] = i;
}

// CHECK: !{!"llvm.loop.mustprogress"}
// CHECK: !{!"llvm.loop.unroll.full"}
// CHECK-NOT: vectorize
// CHECK-NOT: interleave
// CHECK-NOT: distribute
