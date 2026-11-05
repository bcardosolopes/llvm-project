// RUN: %clang_cc1 -triple x86_64-apple-macosx10.7.2 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-apple-macosx10.7.2 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

struct X { int x[6]; };
struct Y { char x[13]; struct X y; } __attribute((packed));
struct Y g;
void f(struct X);
struct X foo(void);

// Test that a byval parameter is used with its byval alignment (align 8),
// not the natural struct alignment (align 4), in memcpy operations.
void test3(struct X a) {
  g.y = a;
}
// CHECK-LABEL: define void @test3(ptr noundef byval(%struct.X) align 8 %0)
// CHECK: call void @llvm.memcpy{{.*}}(ptr align 1 getelementptr inbounds{{.*}}@g{{.*}}, ptr align 8 %0, i64 24, i1 false)

// OGCG-LABEL: define void @test3(ptr noundef byval(%struct.X) align 8 %a)
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 1 getelementptr inbounds{{.*}}@g{{.*}}, ptr align 8 %a, i64 24, i1 false)

// Test that byval argument preparation uses the byval alignment for the
// temporary alloca and memcpy dest, and the packed struct member alignment
// (align 1) for the memcpy source.
void test4(void) {
  f(g.y);
}
// CHECK-LABEL: define void @test4()
// CHECK: %[[TMP:.*]] = alloca %struct.X, i64 1, align 8
// CHECK: call void @llvm.memcpy{{.*}}(ptr align 8 %[[TMP]], ptr align 1 getelementptr inbounds{{.*}}@g{{.*}}, i64 24, i1 false)
// CHECK: call void @f(ptr noundef byval(%struct.X) align 8 %[[TMP]])

// OGCG-LABEL: define void @test4()
// OGCG: %[[TMP:.*]] = alloca %struct.X, align 8
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 8 %[[TMP]], ptr align 1 getelementptr inbounds{{.*}}@g{{.*}}, i64 24, i1 false)
// OGCG: call void @f(ptr noundef byval(%struct.X) align 8 %[[TMP]])

// Verify that assigning a struct returned by value into a packed struct field
// uses a properly-aligned temporary + memcpy, not a direct store into the
// misaligned packed field.
void test6(void) {
  g.y = foo();
}
// CHECK-LABEL: define void @test6()
// CHECK: call void @foo(ptr dead_on_unwind {{.*}} sret(%struct.X) align 4 %[[TMP:.*]])
// CHECK: call void @llvm.memcpy{{.*}}(ptr align 1 getelementptr inbounds{{.*}}@g{{.*}}, ptr align 4 %[[TMP]], i64 24, i1 false)

// OGCG-LABEL: define void @test6()
// OGCG: call void @foo(ptr dead_on_unwind {{.*}} sret(%struct.X) align 4 %[[TMP:.*]])
// OGCG: call void @llvm.memcpy{{.*}}(ptr align 1 getelementptr inbounds{{.*}}@g{{.*}}, ptr align 4 %[[TMP]], i64 24, i1 false)
