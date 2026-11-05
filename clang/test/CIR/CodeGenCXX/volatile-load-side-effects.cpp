// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -Wno-unused-value -std=c++98 %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -Wno-unused-value -std=c++98 %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

// Volatile loads must not be removed even when their result is unused.
// C++ requires that volatile accesses are observable side effects.

volatile int i;
volatile int j;

// Discarded -(i = j): the volatile re-read of i must be preserved.
// LLVM-LABEL: define {{.*}} @_Z14test_discardedv
// LLVM: load volatile i32, ptr @j
// LLVM: store volatile i32 %{{.*}}, ptr @i
// LLVM: load volatile i32, ptr @i
// OGCG-LABEL: define {{.*}} @_Z14test_discardedv
// OGCG: load volatile i32, ptr @j
// OGCG: store volatile i32 %{{.*}}, ptr @i
// OGCG: load volatile i32, ptr @i
void test_discarded() {
  -(i = j);
}

// Discarded (float)(i = i): volatile re-read must be preserved.
// LLVM-LABEL: define {{.*}} @_Z18test_cast_to_floatv
// LLVM: load volatile i32, ptr @i
// LLVM: store volatile i32 %{{.*}}, ptr @i
// LLVM: load volatile i32, ptr @i
// OGCG-LABEL: define {{.*}} @_Z18test_cast_to_floatv
// OGCG: load volatile i32, ptr @i
// OGCG: store volatile i32 %{{.*}}, ptr @i
// OGCG: load volatile i32, ptr @i
void test_cast_to_float() {
  (float)(i = i);
}

// Simple volatile use in discarded context must preserve the load.
// LLVM-LABEL: define {{.*}} @_Z24test_simple_volatile_usev
// LLVM: load volatile i32, ptr @i
// OGCG-LABEL: define {{.*}} @_Z24test_simple_volatile_usev
// OGCG: load volatile i32, ptr @i
void test_simple_volatile_use() {
  i + 0;
}
