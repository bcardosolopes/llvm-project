// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: memcpy for byval argument copies includes alignment.

struct Big { int data[10]; };
void takes_big(struct Big b);

void test(struct Big* p) {
  takes_big(*p);
}

// LLVM-LABEL: define {{.*}} @test
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align {{[0-9]+}} %{{.*}}, ptr align {{[0-9]+}} %{{.*}}, i64 40, i1 false)

// OGCG-LABEL: define {{.*}} @test
// OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align {{[0-9]+}} %{{.*}}, ptr align {{[0-9]+}} %{{.*}}, i64 40, i1 false)
