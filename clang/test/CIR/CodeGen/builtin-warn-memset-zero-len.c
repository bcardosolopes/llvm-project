// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
//
// __warn_memset_zero_len is a no-op builtin used by glibc's <string.h>.
// Verify CIR matches OGCG (empty function body, just ret void).

// LLVM-LABEL: define {{.*}}void @test()
// LLVM-NEXT:    ret void
// LLVM-NEXT:  }

// OGCG-LABEL: define {{.*}}void @test()
// OGCG:         ret void
void test(void) {
  __warn_memset_zero_len();
}
