// RUN: %clang_cc1 -std=c11 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -std=c11 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -std=c11 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.og.ll

// GNU C extension: arithmetic on function pointers (sizeof(function) == 1).

void foo(void);

// CIR-LABEL: @test_post_inc
// LLVM-LABEL: @test_post_inc
// OGCG-LABEL: @test_post_inc
void test_post_inc() {
  void (*p)(void) = foo;
  p++;
  // CIR: %[[LOAD:.*]] = cir.load {{.*}} !cir.ptr<!cir.func<()>>
  // CIR: %[[AMT:.*]] = cir.const #cir.int<1> : !s32i
  // CIR: %[[RES:.*]] = cir.ptr_stride inbounds|nuw %[[LOAD]], %[[AMT]]
  // CIR-SAME: (!cir.ptr<!cir.func<()>>, !s32i) -> !cir.ptr<!cir.func<()>>

  // LLVM: %[[LOAD:.*]] = load ptr
  // LLVM: %[[RES:.*]] = getelementptr inbounds nuw i8, ptr %[[LOAD]], i64 1

  // OGCG: %[[LOAD:.*]] = load ptr
  // OGCG: getelementptr inbounds {{(nuw )?}}i8, ptr %[[LOAD]], i32 1
}

// CIR-LABEL: @test_post_dec
// LLVM-LABEL: @test_post_dec
// OGCG-LABEL: @test_post_dec
void test_post_dec() {
  void (*p)(void) = foo;
  p--;
  // CIR: %[[LOAD:.*]] = cir.load {{.*}} !cir.ptr<!cir.func<()>>
  // CIR: %[[AMT:.*]] = cir.const #cir.int<-1> : !s32i
  // CIR: %[[RES:.*]] = cir.ptr_stride inbounds %[[LOAD]], %[[AMT]]
  // CIR-SAME: (!cir.ptr<!cir.func<()>>, !s32i) -> !cir.ptr<!cir.func<()>>

  // LLVM: %[[LOAD:.*]] = load ptr
  // LLVM: %[[RES:.*]] = getelementptr inbounds i8, ptr %[[LOAD]], i64 -1

  // OGCG: %[[LOAD:.*]] = load ptr
  // OGCG: getelementptr inbounds i8, ptr %[[LOAD]], i32 -1
}

// CIR-LABEL: @test_pre_inc
// LLVM-LABEL: @test_pre_inc
// OGCG-LABEL: @test_pre_inc
void test_pre_inc() {
  void (*p)(void) = foo;
  ++p;
  // CIR: %[[LOAD:.*]] = cir.load {{.*}} !cir.ptr<!cir.func<()>>
  // CIR: %[[AMT:.*]] = cir.const #cir.int<1> : !s32i
  // CIR: %[[RES:.*]] = cir.ptr_stride inbounds|nuw %[[LOAD]], %[[AMT]]
  // CIR-SAME: (!cir.ptr<!cir.func<()>>, !s32i) -> !cir.ptr<!cir.func<()>>

  // LLVM: %[[LOAD:.*]] = load ptr
  // LLVM: %[[RES:.*]] = getelementptr inbounds nuw i8, ptr %[[LOAD]], i64 1

  // OGCG: %[[LOAD:.*]] = load ptr
  // OGCG: getelementptr inbounds {{(nuw )?}}i8, ptr %[[LOAD]], i32 1
}

// CIR-LABEL: @test_pre_dec
// LLVM-LABEL: @test_pre_dec
// OGCG-LABEL: @test_pre_dec
void test_pre_dec() {
  void (*p)(void) = foo;
  --p;
  // CIR: %[[LOAD:.*]] = cir.load {{.*}} !cir.ptr<!cir.func<()>>
  // CIR: %[[AMT:.*]] = cir.const #cir.int<-1> : !s32i
  // CIR: %[[RES:.*]] = cir.ptr_stride inbounds %[[LOAD]], %[[AMT]]
  // CIR-SAME: (!cir.ptr<!cir.func<()>>, !s32i) -> !cir.ptr<!cir.func<()>>

  // LLVM: %[[LOAD:.*]] = load ptr
  // LLVM: %[[RES:.*]] = getelementptr inbounds i8, ptr %[[LOAD]], i64 -1

  // OGCG: %[[LOAD:.*]] = load ptr
  // OGCG: getelementptr inbounds i8, ptr %[[LOAD]], i32 -1
}
