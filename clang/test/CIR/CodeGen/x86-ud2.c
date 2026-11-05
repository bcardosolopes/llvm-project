// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fms-extensions \
// RUN:   -Wno-implicit-function-declaration -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fms-extensions \
// RUN:   -Wno-implicit-function-declaration -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

void test_ud2(void) {
  __ud2();
}
// CHECK-LABEL: @test_ud2
// CHECK: call void @llvm.trap()
// OGCG-LABEL: @test_ud2
// OGCG: call void @llvm.trap()
