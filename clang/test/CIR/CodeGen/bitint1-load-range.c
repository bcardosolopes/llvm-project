// RUN: %clang_cc1 -triple x86_64-linux-gnu -O1 -disable-llvm-passes -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify _BitInt(1) loads use i8 storage type (not i1) to match OGCG.
// Previously CIR created i1 loads with i8 range metadata, causing a
// "Range types must match instruction type" verifier error.

unsigned _BitInt(1) global_b1;

unsigned _BitInt(1) load_b1(void) {
  // CHECK: define {{.*}} @load_b1
  // CHECK: load i8, ptr @global_b1
  // CHECK: trunc i8
  // CHECK: ret i1
  return global_b1;
}
