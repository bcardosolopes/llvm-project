// RUN: %clang_cc1 -no-enable-noundef-analysis -triple x86_64-unknown-linux-gnu -target-cpu core2 -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -no-enable-noundef-analysis -triple aarch64-unknown-linux-gnu -target-cpu generic -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -no-enable-noundef-analysis -triple x86_64-unknown-windows -target-cpu core2 -fclangir -emit-llvm %s -o - | FileCheck %s
//
// direct-CIR swiftcall lowering must match OGCG for indirect-result
// parameter attributes and legalized vector aggregate expansion on supported
// x86_64 and AArch64 targets.

#define SWIFTCALL __attribute__((swiftcall))
#define OUT __attribute__((swift_indirect_result))

typedef float float8 __attribute__((ext_vector_type(8)));

typedef struct {
  char array[1024];
} struct_reallybig;

typedef struct {
  float8 c0;
} struct_vf81;

SWIFTCALL void indirect_result_1(OUT int *arg0, OUT float *arg1) {}
// CHECK-LABEL: define{{.*}} swiftcc void @indirect_result_1(ptr noalias sret(ptr) align 4 dereferenceable(4) %0, ptr noalias align 4 dereferenceable(4) %1)

void test_indirect_result_1(void) {
  int x = 0;
  float y = 0;
  indirect_result_1(&x, &y);
}
// CHECK-LABEL: define{{.*}} void @test_indirect_result_1()
// CHECK: call swiftcc void @indirect_result_1(ptr noalias sret(ptr) align 4 dereferenceable(4) %{{.*}}, ptr noalias align 4 dereferenceable(4) %{{.*}})

SWIFTCALL struct_reallybig indirect_result_3(OUT int *arg0, OUT float *arg1) {
  __builtin_unreachable();
}
// CHECK-LABEL: define{{.*}} swiftcc void @indirect_result_3(ptr dead_on_unwind noalias writable sret(%struct.struct_reallybig) align 1 %0, ptr noalias align 4 dereferenceable(4) %1, ptr noalias align 4 dereferenceable(4) %2)

SWIFTCALL struct_vf81 return_struct_vf81(void) {
  struct_vf81 x = {};
  return x;
}
// CHECK-LABEL: define{{.*}} swiftcc { <4 x float>, <4 x float> } @return_struct_vf81()

SWIFTCALL void take_struct_vf81(struct_vf81 v) {}
// CHECK-LABEL: define{{.*}} swiftcc void @take_struct_vf81(<4 x float> %0, <4 x float> %1)

void test_struct_vf81(void) {
  take_struct_vf81(return_struct_vf81());
}
// CHECK-LABEL: define{{.*}} void @test_struct_vf81()
// CHECK: call swiftcc { <4 x float>, <4 x float> } @return_struct_vf81()
// CHECK: call swiftcc void @take_struct_vf81(<4 x float> %{{.*}}, <4 x float> %{{.*}})
