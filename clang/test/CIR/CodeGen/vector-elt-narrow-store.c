// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

typedef int __v4si __attribute__((__vector_size__(16)));

// Storing a char into an int vector element should sign-extend first.
void test_char_to_int_vec(__v4si *v, char c) {
  (*v)[0] = c;
}
// CHECK-LABEL: @test_char_to_int_vec
// CHECK: sext i8 %{{.*}} to i32
// CHECK: insertelement <4 x i32>
// OGCG-LABEL: @test_char_to_int_vec
// OGCG: sext i8 %{{.*}} to i32
// OGCG: insertelement <4 x i32>

// Storing a short into an int vector element should sign-extend first.
void test_short_to_int_vec(__v4si *v, short s) {
  (*v)[1] = s;
}
// CHECK-LABEL: @test_short_to_int_vec
// CHECK: sext i16 %{{.*}} to i32
// CHECK: insertelement <4 x i32>
// OGCG-LABEL: @test_short_to_int_vec
// OGCG: sext i16 %{{.*}} to i32
// OGCG: insertelement <4 x i32>

// Storing a _Bool into an int vector element should zero-extend first.
void test_bool_to_int_vec(__v4si *v, _Bool b) {
  (*v)[2] = b;
}
// CHECK-LABEL: @test_bool_to_int_vec
// CHECK: zext i1 %{{.*}} to i32
// CHECK: insertelement <4 x i32>
// OGCG-LABEL: @test_bool_to_int_vec
// OGCG: zext i1 %{{.*}} to i32
// OGCG: insertelement <4 x i32>
