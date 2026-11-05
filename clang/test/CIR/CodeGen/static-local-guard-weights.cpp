// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

struct S {
  S();
  ~S();
};

void test() {
  static S s;
}

// The guard check branch should have !prof metadata with weights {1, 1048575}
// to indicate the initialization path is cold.
// LLVM-LABEL: @_Z4testv
// LLVM: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !prof [[WEIGHTS:![0-9]+]]
// LLVM: [[WEIGHTS]] = !{!"branch_weights", i32 1, i32 1048575}

// OGCG-LABEL: @_Z4testv
// OGCG: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !prof [[WEIGHTS:![0-9]+]]
// OGCG: [[WEIGHTS]] = !{!"branch_weights", i32 1, i32 1048575}
