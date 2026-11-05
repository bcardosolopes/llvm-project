// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=gnu11 \
// RUN:     -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=gnu11 \
// RUN:     -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

union U1 {
  int x;
  char y[5];
};

struct S1 {
  int x;
  long long y;
};

struct S2 {
  unsigned char b1 : 3;
  unsigned char    : 2;
  unsigned char b2 : 6;
  unsigned char b3 : 2;
  int i;
};

struct S3 {
  int x;
} __attribute__((__aligned__(8)));

struct S4 {
  int a;
  union U1 b;
};

struct S5 {
  char x;
  unsigned char y : 4;
  unsigned char z : 7;
} __attribute__((packed));

// Test non-const initializer for union with padding.
// CHECK-LABEL: define dso_local void @test1(
// CHECK:         store i32 %{{.*}}, ptr %{{.*}}, align 4
// CHECK:         %[[GEP:.*]] = getelementptr {{.*}}i8, ptr %{{.*}}, i64 4
// CHECK:         call void @llvm.memset.p0.i64(ptr %[[GEP]], i8 0, i64 4, i1 false)
// CHECK:         ret void

// OGCG-LABEL: define dso_local void @test1(
// OGCG:         store i32 %{{.*}}, ptr %{{.*}}, align 4
// OGCG:         %[[GEP:.*]] = getelementptr i8, ptr %{{.*}}, i64 4
// OGCG:         call void @llvm.memset.p0.i64(ptr align 4 %[[GEP]], i8 0, i64 4, i1 false)
// OGCG:         ret void
void test1(int x) {
  union U1 a = {x};
}

// Test non-const initializer for struct with padding between fields.
// CHECK-LABEL: define dso_local void @test2(
// CHECK:         store i32 0, ptr %{{.*}}
// CHECK:         %[[GEP:.*]] = getelementptr {{.*}}i8, ptr %{{.*}}, i64 4
// CHECK:         call void @llvm.memset.p0.i64(ptr %[[GEP]], i8 0, i64 4, i1 false)
// CHECK:         store i64 %{{.*}}, ptr %{{.*}}
// CHECK:         ret void

// OGCG-LABEL: define dso_local void @test2(
// OGCG:         store i32 0, ptr %{{.*}}
// OGCG:         %[[GEP:.*]] = getelementptr i8, ptr %{{.*}}, i64 4
// OGCG:         call void @llvm.memset.p0.i64(ptr align 4 %[[GEP]], i8 0, i64 4, i1 false)
// OGCG:         store i64 %{{.*}}, ptr %{{.*}}
// OGCG:         ret void
void test2(long long y) {
  struct S1 s = {.y = y};
}

// Test non-const initializer for struct with bitfield padding.
// CHECK-LABEL: define dso_local void @test3(
// CHECK:         store i16 0, ptr %{{.*}}
// CHECK:         %[[GEP:.*]] = getelementptr {{.*}}i8, ptr %{{.*}}, i64 2
// CHECK:         call void @llvm.memset.p0.i64(ptr %[[GEP]], i8 0, i64 2, i1 false)
// CHECK:         store i32 0, ptr %{{.*}}
// CHECK:         ret void

// OGCG-LABEL: define dso_local void @test3(
// OGCG:         store i16 0, ptr %{{.*}}
// OGCG:         %[[GEP:.*]] = getelementptr i8, ptr %{{.*}}, i64 2
// OGCG:         call void @llvm.memset.p0.i64(ptr align 2 %[[GEP]], i8 0, i64 2, i1 false)
// OGCG:         store i32 0, ptr %{{.*}}
// OGCG:         ret void
void test3(unsigned char b) {
  struct S2 s = {.b1 = b};
}

// Test non-const initializer for struct with trailing padding.
// CHECK-LABEL: define dso_local void @test4(
// CHECK:         store i32 %{{.*}}, ptr %{{.*}}
// CHECK:         %[[GEP:.*]] = getelementptr {{.*}}i8, ptr %{{.*}}, i64 4
// CHECK:         call void @llvm.memset.p0.i64(ptr %[[GEP]], i8 0, i64 4, i1 false)
// CHECK:         ret void

// OGCG-LABEL: define dso_local void @test4(
// OGCG:         store i32 %{{.*}}, ptr %{{.*}}
// OGCG:         %[[GEP:.*]] = getelementptr i8, ptr %{{.*}}, i64 4
// OGCG:         call void @llvm.memset.p0.i64(ptr align 4 %[[GEP]], i8 0, i64 4, i1 false)
// OGCG:         ret void
void test4(int x) {
  struct S3 s = {x};
}

// Test non-const initializer for union in struct.
// CHECK-LABEL: define dso_local void @test5(
// CHECK:         %[[UNION:.*]] = getelementptr inbounds nuw %struct.S4, ptr %{{.*}}, i32 0, i32 1
// CHECK:         store i32 %{{.*}}, ptr %[[UNION]], align 4
// CHECK:         %[[GEP:.*]] = getelementptr {{.*}}i8, ptr %[[UNION]], i64 4
// CHECK:         call void @llvm.memset.p0.i64(ptr %[[GEP]], i8 0, i64 4, i1 false)
// CHECK:         ret void

// OGCG-LABEL: define dso_local void @test5(
// OGCG:         %[[UNION:.*]] = getelementptr inbounds nuw %struct.S4, ptr %{{.*}}, i32 0, i32 1
// OGCG:         store i32 %{{.*}}, ptr %[[UNION]], align 4
// OGCG:         %[[GEP:.*]] = getelementptr i8, ptr %[[UNION]], i64 4
// OGCG:         call void @llvm.memset.p0.i64(ptr align 4 %[[GEP]], i8 0, i64 4, i1 false)
// OGCG:         ret void
void test5(int a, int b) {
  struct S4 s = {a, {b}};
}

// Test packed struct with bitfields - zeroing of bitfield storage.
// CHECK-LABEL: define dso_local void @test6(
// CHECK:         store i8 %{{.*}}, ptr %{{.*}}
// CHECK:         store i16 0, ptr %{{.*}}
// CHECK:         ret void

// OGCG-LABEL: define dso_local void @test6(
// OGCG:         store i8 %{{.*}}, ptr %{{.*}}
// OGCG:         store i16 0, ptr %{{.*}}
// OGCG:         ret void
void test6(char x) {
  struct S5 s = {.x = x};
}
