// RUN: %clang_cc1 -triple x86_64-pc-win32 -fms-extensions -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-pc-win32 -fms-extensions -fclangir -emit-llvm -o - %s | FileCheck %s

struct Large {
  int a[5];
};

void __vectorcall indirect_large(int a, struct Large b, int c) {}
// CHECK-LABEL: define dso_local x86_vectorcallcc void @"\01indirect_large@@40"
// CHECK-SAME: (i32 noundef %{{[^,]+}}, ptr dead_on_return noundef %{{[^,]+}}, i32 noundef %{{[^)]+}})

struct HFA4 {
  double a, b, c, d;
};

void __vectorcall inreg_hfa(int a, struct HFA4 b, int c) {}
// CHECK-LABEL: define dso_local x86_vectorcallcc void @"\01inreg_hfa@@48"
// CHECK-SAME: (i32 noundef %{{[^,]+}}, %struct.HFA4 inreg %{{[^,]+}}, i32 noundef %{{[^)]+}})

typedef float v4f32 __attribute__((vector_size(16)));

struct HVA5 {
  v4f32 a, b, c, d, e;
};

struct HVA5 __vectorcall sret_hva(void) {
  struct HVA5 x = {0};
  return x;
}
// CHECK-LABEL: define dso_local x86_vectorcallcc void @"\01sret_hva@@0"
// CHECK-SAME: (ptr dead_on_unwind noalias writable sret(%struct.HVA5) align 16 %{{[^)]+}})

void caller(struct Large l, struct HFA4 h) {
  indirect_large(1, l, 2);
  inreg_hfa(3, h, 4);
  (void)sret_hva();
}
// CHECK-LABEL: define dso_local void @caller(
// CHECK: call x86_vectorcallcc void @"\01indirect_large@@40"(i32 noundef 1, ptr dead_on_return noundef %{{[^,]+}}, i32 noundef 2)
// CHECK: call x86_vectorcallcc void @"\01inreg_hfa@@48"(i32 noundef 3, %struct.HFA4 inreg %{{[^,]+}}, i32 noundef 4)
// CHECK: call x86_vectorcallcc void @"\01sret_hva@@0"(ptr dead_on_unwind writable sret(%struct.HVA5) align 16 %{{[^)]+}})
