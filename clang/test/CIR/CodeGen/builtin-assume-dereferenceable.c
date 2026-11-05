// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

void test_assume_dereferenceable(int *p) {
  __builtin_assume_dereferenceable(p, 4);
  *p = 42;
}

// LLVM-LABEL: define {{.*}}void @test_assume_dereferenceable
// LLVM: call void @llvm.assume(i1 true) [ "dereferenceable"(ptr {{%.*}}, i64 4) ]

// OGCG-LABEL: define {{.*}}void @test_assume_dereferenceable
// OGCG: call void @llvm.assume(i1 true) [ "dereferenceable"(ptr {{%.*}}, i64 4) ]

void test_assume_dereferenceable_large(char *p) {
  __builtin_assume_dereferenceable(p, 1024);
}

// LLVM-LABEL: define {{.*}}void @test_assume_dereferenceable_large
// LLVM: call void @llvm.assume(i1 true) [ "dereferenceable"(ptr {{%.*}}, i64 1024) ]

// OGCG-LABEL: define {{.*}}void @test_assume_dereferenceable_large
// OGCG: call void @llvm.assume(i1 true) [ "dereferenceable"(ptr {{%.*}}, i64 1024) ]
