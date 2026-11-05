// RUN: %clang_cc1 -triple i386-pc-win32 -ffreestanding -fms-extensions -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple i386-pc-win32 -ffreestanding -fms-extensions -fclangir -emit-llvm -o - %s | FileCheck %s

struct Large {
  int a[5];
};

void __vectorcall indirect_large(int a, struct Large b, int c) {}
// CHECK-LABEL: define dso_local x86_vectorcallcc void @"\01indirect_large@@28"
// CHECK-SAME: (i32 inreg noundef %{{[^,]+}}, ptr noundef byval(%struct.Large) align 4 %{{[^,]+}}, i32 inreg noundef %{{[^)]+}})

struct HFA4 {
  double a, b, c, d;
};

void __vectorcall mixed_hfa(struct HFA4 a, struct HFA4 b, double c) {}
// CHECK-LABEL: define dso_local x86_vectorcallcc void @"\01mixed_hfa@@72"
// CHECK-SAME: (%struct.HFA4 inreg %{{[^,]+}}, ptr dead_on_return inreg noundef %{{[^,]+}}, double inreg noundef %{{[^)]+}})

typedef float v4f32 __attribute__((vector_size(16)));

struct HVA4 {
  v4f32 a, b, c, d;
};

v4f32 __vectorcall mixed_hva(struct HVA4 a, struct HVA4 b, v4f32 c) {
  return c;
}
// CHECK-LABEL: define dso_local x86_vectorcallcc <4 x float> @"\01mixed_hva@@144"
// CHECK-SAME: (%struct.HVA4 inreg %{{[^,]+}}, ptr dead_on_return inreg noundef %{{[^,]+}}, <4 x float> inreg noundef %{{[^)]+}})

struct HVA5 {
  v4f32 a, b, c, d, e;
};

struct HVA5 __vectorcall sret_hva(void) {
  struct HVA5 x = {0};
  return x;
}
// CHECK-LABEL: define dso_local x86_vectorcallcc void @"\01sret_hva@@0"
// CHECK-SAME: (ptr dead_on_unwind noalias writable sret(%struct.HVA5) align 16 %{{[^)]+}})

void __vectorcall exhausted_vectors(
    double xmm0, double xmm1, double xmm2, double xmm3, double xmm4,
    v4f32 xmm5, v4f32 ecx, int edx, v4f32 mem) {}
// CHECK-LABEL: define dso_local x86_vectorcallcc void @"\01exhausted_vectors@@92"
// CHECK-SAME: (double inreg noundef %{{[^,]+}},
// CHECK-SAME: double inreg noundef %{{[^,]+}},
// CHECK-SAME: double inreg noundef %{{[^,]+}},
// CHECK-SAME: double inreg noundef %{{[^,]+}},
// CHECK-SAME: double inreg noundef %{{[^,]+}},
// CHECK-SAME: <4 x float> inreg noundef %{{[^,]+}},
// CHECK-SAME: ptr dead_on_return inreg noundef %{{[^,]+}},
// CHECK-SAME: i32 inreg noundef %{{[^,]+}},
// CHECK-SAME: ptr dead_on_return noundef %{{[^)]+}})

void caller(struct Large l, struct HFA4 hfa, struct HVA4 hva, v4f32 v) {
  indirect_large(1, l, 2);
  mixed_hfa(hfa, hfa, 3.0);
  (void)mixed_hva(hva, hva, v);
  exhausted_vectors(1.0, 2.0, 3.0, 4.0, 5.0, v, v, 7, v);
  (void)sret_hva();
}
// CHECK-LABEL: define dso_local void @caller(
// CHECK: call x86_vectorcallcc void @"\01indirect_large@@28"(i32 inreg noundef 1, ptr noundef byval(%struct.Large) align 4 %{{[^,]+}}, i32 inreg noundef 2)
// CHECK: call x86_vectorcallcc void @"\01mixed_hfa@@72"(%struct.HFA4 inreg %{{[^,]+}}, ptr dead_on_return inreg noundef %{{[^,]+}}, double inreg noundef 3.000000e+00)
// CHECK: call x86_vectorcallcc <4 x float> @"\01mixed_hva@@144"(%struct.HVA4 inreg %{{[^,]+}}, ptr dead_on_return inreg noundef %{{[^,]+}}, <4 x float> inreg noundef %{{[^)]+}})
// CHECK: call x86_vectorcallcc void @"\01exhausted_vectors@@92"(double inreg noundef 1.000000e+00, double inreg noundef 2.000000e+00, double inreg noundef 3.000000e+00, double inreg noundef 4.000000e+00, double inreg noundef 5.000000e+00, <4 x float> inreg noundef %{{[^,]+}}, ptr dead_on_return inreg noundef %{{[^,]+}}, i32 inreg noundef 7, ptr dead_on_return noundef %{{[^)]+}})
// CHECK: call x86_vectorcallcc void @"\01sret_hva@@0"(ptr dead_on_unwind writable sret(%struct.HVA5) align 16 %{{[^)]+}})
