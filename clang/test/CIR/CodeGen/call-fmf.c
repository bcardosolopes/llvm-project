// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fmath-errno -fclangir \
// RUN:   -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fmath-errno \
// RUN:   -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

float sqrtf(float);
double sqrt(double);

#pragma float_control(precise, off)

float test_call_fast(float x) {
  return sqrtf(x);
}

// LLVM-LABEL: define {{.*}}float @test_call_fast
// LLVM: call fast float @sqrtf(float {{.*}})

// OGCG-LABEL: define {{.*}}float @test_call_fast
// OGCG: call fast float @sqrtf(float {{.*}})

double test_call_fast_double(double x) {
  return sqrt(x);
}

// LLVM-LABEL: define {{.*}}double @test_call_fast_double
// LLVM: call fast double @sqrt(double {{.*}})

// OGCG-LABEL: define {{.*}}double @test_call_fast_double
// OGCG: call fast double @sqrt(double {{.*}})

#pragma float_control(precise, on)

float test_call_no_fast(float x) {
  return sqrtf(x);
}

// Verify that calls outside the fast region do NOT have fast flags.
// LLVM-LABEL: define {{.*}}float @test_call_no_fast
// LLVM: call float @sqrtf(float {{.*}})
// LLVM-NOT: fast

// OGCG-LABEL: define {{.*}}float @test_call_no_fast
// OGCG: call float @sqrtf(float {{.*}})
// OGCG-NOT: fast
