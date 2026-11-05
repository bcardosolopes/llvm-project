// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fms-extensions -emit-llvm -fclangir %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fms-extensions -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s -check-prefix=OGCG

void __writex18byte(unsigned int offset, unsigned char data);
void __writex18word(unsigned int offset, unsigned short data);
void __writex18dword(unsigned int offset, unsigned int data);
void __writex18qword(unsigned int offset, unsigned long long data);

unsigned char __readx18byte(unsigned int offset);
unsigned short __readx18word(unsigned int offset);
unsigned int __readx18dword(unsigned int offset);
unsigned long long __readx18qword(unsigned int offset);

void __addx18byte(unsigned int offset, unsigned char data);
void __addx18word(unsigned int offset, unsigned short data);
void __addx18dword(unsigned int offset, unsigned int data);
void __addx18qword(unsigned int offset, unsigned long long data);

void __incx18byte(unsigned int offset);
void __incx18word(unsigned int offset);
void __incx18dword(unsigned int offset);
void __incx18qword(unsigned int offset);

void test_writex18byte(unsigned int offset, unsigned char data) {
  __writex18byte(offset, data);
}

// CIR: define {{.*}}void @test_writex18byte(i32 {{.*}}%{{.*}}, i8 {{.*}}%{{.*}})
// CIR:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG:.*]])
// CIR:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// CIR:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// CIR:   store i8 %{{.*}}, ptr %[[ADDR]], align 1

// OGCG: define {{.*}}void @test_writex18byte(i32 {{.*}}%{{.*}}, i8 {{.*}}%{{.*}})
// OGCG:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG:.*]])
// OGCG:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// OGCG:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// OGCG:   store i8 %{{.*}}, ptr %[[ADDR]], align 1

void test_writex18qword(unsigned int offset, unsigned long long data) {
  __writex18qword(offset, data);
}

// CIR: define {{.*}}void @test_writex18qword(i32 {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// CIR:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG]])
// CIR:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// CIR:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// CIR:   store i64 %{{.*}}, ptr %[[ADDR]], align 1

// OGCG: define {{.*}}void @test_writex18qword(i32 {{.*}}%{{.*}}, i64 {{.*}}%{{.*}})
// OGCG:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG]])
// OGCG:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// OGCG:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// OGCG:   store i64 %{{.*}}, ptr %[[ADDR]], align 1

unsigned char test_readx18byte(unsigned int offset) {
  return __readx18byte(offset);
}

// CIR: define {{.*}}i8 @test_readx18byte(i32 {{.*}}%{{.*}})
// CIR:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG]])
// CIR:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// CIR:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// CIR:   %{{.*}} = load i8, ptr %[[ADDR]], align 1

// OGCG: define {{.*}}i8 @test_readx18byte(i32 {{.*}}%{{.*}})
// OGCG:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG]])
// OGCG:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// OGCG:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// OGCG:   %{{.*}} = load i8, ptr %[[ADDR]], align 1

unsigned long long test_readx18qword(unsigned int offset) {
  return __readx18qword(offset);
}

// CIR: define {{.*}}i64 @test_readx18qword(i32 {{.*}}%{{.*}})
// CIR:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG]])
// CIR:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// CIR:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// CIR:   %{{.*}} = load i64, ptr %[[ADDR]], align 1

// OGCG: define {{.*}}i64 @test_readx18qword(i32 {{.*}}%{{.*}})
// OGCG:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG]])
// OGCG:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// OGCG:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// OGCG:   %{{.*}} = load i64, ptr %[[ADDR]], align 1

void test_addx18byte(unsigned int offset, unsigned char data) {
  __addx18byte(offset, data);
}

// CIR: define {{.*}}void @test_addx18byte(i32 {{.*}}%{{.*}}, i8 {{.*}}%{{.*}})
// CIR:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG]])
// CIR:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// CIR:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// CIR:   %[[LOADED:.*]] = load i8, ptr %[[ADDR]], align 1
// CIR:   %[[SUM:.*]] = add i8 %[[LOADED]], %{{.*}}
// CIR:   store i8 %[[SUM]], ptr %[[ADDR]], align 1

// OGCG: define {{.*}}void @test_addx18byte(i32 {{.*}}%{{.*}}, i8 {{.*}}%{{.*}})
// OGCG:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG]])
// OGCG:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// OGCG:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// OGCG:   %[[LOADED:.*]] = load i8, ptr %[[ADDR]], align 1
// OGCG:   %[[SUM:.*]] = add i8 %[[LOADED]], %{{.*}}
// OGCG:   store i8 %[[SUM]], ptr %[[ADDR]], align 1

void test_incx18byte(unsigned int offset) {
  __incx18byte(offset);
}

// CIR: define {{.*}}void @test_incx18byte(i32 {{.*}}%{{.*}})
// CIR:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG]])
// CIR:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// CIR:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// CIR:   %[[LOADED:.*]] = load i8, ptr %[[ADDR]], align 1
// CIR:   %[[SUM:.*]] = add i8 %[[LOADED]], 1
// CIR:   store i8 %[[SUM]], ptr %[[ADDR]], align 1

// OGCG: define {{.*}}void @test_incx18byte(i32 {{.*}}%{{.*}})
// OGCG:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG]])
// OGCG:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// OGCG:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// OGCG:   %[[LOADED:.*]] = load i8, ptr %[[ADDR]], align 1
// OGCG:   %[[SUM:.*]] = add i8 %[[LOADED]], 1
// OGCG:   store i8 %[[SUM]], ptr %[[ADDR]], align 1

void test_incx18qword(unsigned int offset) {
  __incx18qword(offset);
}

// CIR: define {{.*}}void @test_incx18qword(i32 {{.*}}%{{.*}})
// CIR:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG]])
// CIR:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// CIR:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// CIR:   %[[LOADED:.*]] = load i64, ptr %[[ADDR]], align 1
// CIR:   %[[SUM:.*]] = add i64 %[[LOADED]], 1
// CIR:   store i64 %[[SUM]], ptr %[[ADDR]], align 1

// OGCG: define {{.*}}void @test_incx18qword(i32 {{.*}}%{{.*}})
// OGCG:   %[[X18:.*]] = call i64 @llvm.read_register.i64(metadata ![[X18REG]])
// OGCG:   %[[PTR:.*]] = inttoptr i64 %[[X18]] to ptr
// OGCG:   %[[ADDR:.*]] = getelementptr {{.*}}i8, ptr %[[PTR]], i64 %{{.*}}
// OGCG:   %[[LOADED:.*]] = load i64, ptr %[[ADDR]], align 1
// OGCG:   %[[SUM:.*]] = add i64 %[[LOADED]], 1
// OGCG:   store i64 %[[SUM]], ptr %[[ADDR]], align 1

// CIR: ![[X18REG]] = !{!"x18"}
// OGCG: ![[X18REG]] = !{!"x18"}
