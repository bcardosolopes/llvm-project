// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that coerced parameter allocas respect typedef-level and field-level
// __attribute__((aligned(N))), matching OGCG behavior.

// Case 1: typedef-level __aligned__(16) on struct with natural align 8.
// Both allocas must be align 16 (not 8).
typedef __attribute__((aligned(16))) struct f22s {
  unsigned long long x[2];
} L;

// CIR-LABEL: define {{.*}} @f22(i64 %0, i64 %1)
// CIR: alloca %struct.f22s, i64 1, align 16
// CIR: store i64 %0, ptr %{{.*}}, align 16

// OGCG-LABEL: define {{.*}} @f22(i64 %{{.*}}, i64 %{{.*}})
// OGCG: alloca %struct.f22s, align 16
// OGCG: store i64 %{{.*}}, ptr %{{.*}}, align 16
void f22(L x) {}

// Case 2: field-level __aligned__(16) raises struct alignment.
struct s12_1 {
  int a __attribute__((aligned(16)));
};

// CIR-LABEL: define {{.*}} @f12_1(i32 %0)
// CIR: alloca %struct.s12_1, i64 1, align 16
// CIR: store i32 %0, ptr %{{.*}}, align 16

// OGCG-LABEL: define {{.*}} @f12_1(i32 %{{.*}})
// OGCG: alloca %struct.s12_1, align 16
// OGCG: store i32 %{{.*}}, ptr %{{.*}}, align 16
void f12_1(struct s12_1 x) {}

// Case 3: struct with natural align 8 (no extra alignment attrs).
typedef struct { long long a; int b; } T23;

// CIR-LABEL: define {{.*}} @f23(i64 %0, i32 %1)
// CIR: alloca %struct.T23, i64 1, align 8
// CIR: store i64 %0, ptr %{{.*}}, align 8

// OGCG-LABEL: define {{.*}} @f23(i64 %{{.*}}, i32 %{{.*}})
// OGCG: alloca %struct.T23, align 8
// OGCG: store i64 %{{.*}}, ptr %{{.*}}, align 8
void f23(T23 x) {}

// Case 4: struct with natural align 4, coerced to i64 — verify alignment
// stays at 4, NOT raised to coerce type alignment.
struct CompleteS {
  int a;
  signed char b;
};

// CIR-LABEL: define {{.*}} @f_completeS(i64 %0)
// CIR: alloca %struct.CompleteS, i64 1, align 4
// CIR: store i64 %0, ptr %{{.*}}, align 4

// OGCG-LABEL: define {{.*}} @f_completeS(i64 %{{.*}})
// OGCG: alloca %struct.CompleteS, align 4
// OGCG: store i64 %{{.*}}, ptr %{{.*}}, align 4
void f_completeS(struct CompleteS x) {}
