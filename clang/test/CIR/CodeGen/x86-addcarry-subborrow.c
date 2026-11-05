// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +adx \
// RUN:   -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -target-feature +adx \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Add-with-carry 32-bit
unsigned char test_addcarryx_u32(unsigned char cf, unsigned int a,
                                 unsigned int b, unsigned int *out) {
  return __builtin_ia32_addcarryx_u32(cf, a, b, out);
}
// CHECK-LABEL: @test_addcarryx_u32
// CHECK: call { i8, i32 } @llvm.x86.addcarry.32(i8 %{{.*}}, i32 %{{.*}}, i32 %{{.*}})
// CHECK: extractvalue { i8, i32 } %{{.*}}, 1
// CHECK: store i32 %{{.*}}, ptr %{{.*}}
// CHECK: extractvalue { i8, i32 } %{{.*}}, 0
// OGCG-LABEL: @test_addcarryx_u32
// OGCG: call { i8, i32 } @llvm.x86.addcarry.32(i8 %{{.*}}, i32 %{{.*}}, i32 %{{.*}})
// OGCG: extractvalue { i8, i32 } %{{.*}}, 1
// OGCG: store i32 %{{.*}}, ptr %{{.*}}
// OGCG: extractvalue { i8, i32 } %{{.*}}, 0

// Add-with-carry 64-bit
unsigned char test_addcarryx_u64(unsigned char cf, unsigned long long a,
                                 unsigned long long b,
                                 unsigned long long *out) {
  return __builtin_ia32_addcarryx_u64(cf, a, b, out);
}
// CHECK-LABEL: @test_addcarryx_u64
// CHECK: call { i8, i64 } @llvm.x86.addcarry.64(i8 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
// CHECK: extractvalue { i8, i64 } %{{.*}}, 1
// CHECK: store i64 %{{.*}}, ptr %{{.*}}
// CHECK: extractvalue { i8, i64 } %{{.*}}, 0
// OGCG-LABEL: @test_addcarryx_u64
// OGCG: call { i8, i64 } @llvm.x86.addcarry.64(i8 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
// OGCG: extractvalue { i8, i64 } %{{.*}}, 1
// OGCG: store i64 %{{.*}}, ptr %{{.*}}
// OGCG: extractvalue { i8, i64 } %{{.*}}, 0

// Subtract-with-borrow 32-bit
unsigned char test_subborrow_u32(unsigned char cf, unsigned int a,
                                 unsigned int b, unsigned int *out) {
  return __builtin_ia32_subborrow_u32(cf, a, b, out);
}
// CHECK-LABEL: @test_subborrow_u32
// CHECK: call { i8, i32 } @llvm.x86.subborrow.32(i8 %{{.*}}, i32 %{{.*}}, i32 %{{.*}})
// CHECK: extractvalue { i8, i32 } %{{.*}}, 1
// CHECK: store i32 %{{.*}}, ptr %{{.*}}
// CHECK: extractvalue { i8, i32 } %{{.*}}, 0
// OGCG-LABEL: @test_subborrow_u32
// OGCG: call { i8, i32 } @llvm.x86.subborrow.32(i8 %{{.*}}, i32 %{{.*}}, i32 %{{.*}})
// OGCG: extractvalue { i8, i32 } %{{.*}}, 1
// OGCG: store i32 %{{.*}}, ptr %{{.*}}
// OGCG: extractvalue { i8, i32 } %{{.*}}, 0

// Subtract-with-borrow 64-bit
unsigned char test_subborrow_u64(unsigned char cf, unsigned long long a,
                                 unsigned long long b,
                                 unsigned long long *out) {
  return __builtin_ia32_subborrow_u64(cf, a, b, out);
}
// CHECK-LABEL: @test_subborrow_u64
// CHECK: call { i8, i64 } @llvm.x86.subborrow.64(i8 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
// CHECK: extractvalue { i8, i64 } %{{.*}}, 1
// CHECK: store i64 %{{.*}}, ptr %{{.*}}
// CHECK: extractvalue { i8, i64 } %{{.*}}, 0
// OGCG-LABEL: @test_subborrow_u64
// OGCG: call { i8, i64 } @llvm.x86.subborrow.64(i8 %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
// OGCG: extractvalue { i8, i64 } %{{.*}}, 1
// OGCG: store i64 %{{.*}}, ptr %{{.*}}
// OGCG: extractvalue { i8, i64 } %{{.*}}, 0
