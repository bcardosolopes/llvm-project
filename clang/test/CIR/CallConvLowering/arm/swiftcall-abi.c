// RUN: %clang_cc1 -no-enable-noundef-analysis -triple armv7-apple-darwin9 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -no-enable-noundef-analysis -triple armv7-apple-darwin9 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// ARM direct-CIR must run swiftcall ABI legalization before LLVM emission.
// This covers the arm-swiftcall.c failures where we previously skipped target
// lowering entirely and left source-shaped aggregate signatures and invalid
// swifterror uses in the IR.

#define SWIFTCALL __attribute__((swiftcall))
#define OUT __attribute__((swift_indirect_result))
#define ERROR __attribute__((swift_error_result))
#define CONTEXT __attribute__((swift_context))

typedef struct {
  char array[1024];
} struct_reallybig;

typedef struct {
  int x;
  char c0;
  char c1;
  float f0;
  float f1;
} struct_1;

typedef struct {
  double x0;
  double x1;
  double x2;
  double x3;
  double x4;
} struct_d5;

SWIFTCALL void indirect_result_1(OUT int *arg0, OUT float *arg1) {}
// CIR-LABEL: define swiftcc void @indirect_result_1(ptr noalias sret(ptr) align 4 dereferenceable(4) %{{.*}}, ptr noalias align 4 dereferenceable(4) %{{.*}})
// OGCG-LABEL: define swiftcc void @indirect_result_1(ptr noalias sret(ptr) align 4 dereferenceable(4) %{{.*}}, ptr noalias align 4 dereferenceable(4) %{{.*}})

SWIFTCALL struct_reallybig indirect_result_3(OUT int *arg0, OUT float *arg1) {
  __builtin_unreachable();
}
// CIR-LABEL: define swiftcc void @indirect_result_3(ptr dead_on_unwind noalias writable sret(%struct.struct_reallybig) align 1 %{{.*}}, ptr noalias align 4 dereferenceable(4) %{{.*}}, ptr noalias align 4 dereferenceable(4) %{{.*}})
// OGCG-LABEL: define swiftcc void @indirect_result_3(ptr dead_on_unwind noalias writable sret(%struct.struct_reallybig) align 1 %{{.*}}, ptr noalias align 4 dereferenceable(4) %{{.*}}, ptr noalias align 4 dereferenceable(4) %{{.*}})

SWIFTCALL struct_1 return_struct_1(void) {
  struct_1 result = {};
  return result;
}
// CIR-LABEL: define swiftcc { i32, i16, float, float } @return_struct_1()
// OGCG-LABEL: define swiftcc { i32, i16, float, float } @return_struct_1()

SWIFTCALL struct_d5 return_struct_d5(void) {
  struct_d5 result = {};
  return result;
}
// CIR-LABEL: define swiftcc void @return_struct_d5(ptr dead_on_unwind noalias writable sret(%struct.struct_d5) align 4 %{{.*}})
// OGCG-LABEL: define swiftcc void @return_struct_d5(ptr dead_on_unwind noalias writable sret(%struct.struct_d5) align 4 %{{.*}})

SWIFTCALL void take_struct_1(struct_1 v) {}
// CIR-LABEL: define swiftcc void @take_struct_1(i32 %{{.*}}, i16 %{{.*}}, float %{{.*}}, float %{{.*}})
// OGCG-LABEL: define swiftcc void @take_struct_1(i32 %{{.*}}, i16 %{{.*}}, float %{{.*}}, float %{{.*}})

void test_struct_1(void) {
  take_struct_1(return_struct_1());
}
// CIR-LABEL: define {{.*}} @test_struct_1()
// CIR: call swiftcc { i32, i16, float, float } @return_struct_1()
// CIR: call swiftcc void @take_struct_1(i32 %{{.*}}, i16 %{{.*}}, float %{{.*}}, float %{{.*}})
// OGCG-LABEL: define {{.*}} @test_struct_1()
// OGCG: call swiftcc { i32, i16, float, float } @return_struct_1()
// OGCG: call swiftcc void @take_struct_1(i32 %{{.*}}, i16 %{{.*}}, float %{{.*}}, float %{{.*}})

SWIFTCALL void context_error_1(CONTEXT int *self, ERROR float **error) {}
// CIR-LABEL: define swiftcc void @context_error_1(ptr swiftself %{{.*}}, ptr swifterror %{{.*}})
// CIR: alloca ptr
// OGCG-LABEL: define swiftcc void @context_error_1(ptr swiftself %{{.*}}, ptr swifterror %{{.*}})
// OGCG: alloca ptr

void test_context_error_1(void) {
  int x;
  float *error;
  context_error_1(&x, &error);
}
// CIR-LABEL: define {{.*}} @test_context_error_1()
// CIR: alloca swifterror ptr
// CIR: call swiftcc void @context_error_1(ptr swiftself %{{.*}}, ptr swifterror %{{.*}})
// OGCG-LABEL: define {{.*}} @test_context_error_1()
// OGCG: alloca swifterror ptr
// OGCG: call swiftcc void @context_error_1(ptr swiftself %{{.*}}, ptr swifterror %{{.*}})
