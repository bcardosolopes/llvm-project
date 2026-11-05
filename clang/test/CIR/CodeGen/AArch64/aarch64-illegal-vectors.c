// RUN: %clang_cc1 -triple aarch64 -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll %s

// REQUIRES: aarch64-registered-target

typedef float __attribute__((ext_vector_type(3))) float3;
typedef float __attribute__((ext_vector_type(1))) float1;
typedef double __attribute__((ext_vector_type(4))) double4;
typedef float __attribute__((ext_vector_type(8))) float8;

void use_f3(float3 *);
void use_f1(float1 *);
void use_d4(double4 *);
void use_f8(float8 *);

// Non-power-of-2 vector (3 elements): coerced to <4 x i32>.
// The coercion introduces undef bits, so no noundef.
// CHECK: define dso_local void @test_float3_arg(<4 x i32> %0)
void test_float3_arg(float3 v) {
    use_f3(&v);
}

// Single-element 32-bit vector: coerced to i32.
// CHECK: define dso_local void @test_float1_arg(i32 noundef %0)
void test_float1_arg(float1 v) {
    use_f1(&v);
}

// Large vector arg (256-bit, >128): passed indirectly via pointer.
// CHECK: define dso_local void @test_double4_arg(ptr dead_on_return noundef %0)
// CHECK:   %[[TMP:.*]] = alloca <4 x double>, i64 1, align 16
// CHECK:   %[[VAL:.*]] = load <4 x double>, ptr %0, align 16
// CHECK:   store <4 x double> %[[VAL]], ptr %[[TMP]], align 16
void test_double4_arg(double4 v) {
    use_d4(&v);
}

// Large vector return (256-bit): returned via sret.
float8 test_float8_ret(void);
void test_float8_call(void) {
    float8 v = test_float8_ret();
    use_f8(&v);
}
// CHECK: define dso_local void @test_float8_call()
// CHECK:   call void @test_float8_ret(ptr {{.*}}sret(<8 x float>)
