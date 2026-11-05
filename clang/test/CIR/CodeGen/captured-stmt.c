// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test CapturedStmt emission (#pragma clang __debug captured).

int printf(const char *, ...);
int g = 10;

// CIR: cir.func no_inline optnone internal private @__captured_stmt
// CIR:   %[[CTX:.*]] = cir.alloca !cir.ptr<!rec_anon
// CIR:   %[[X:.*]] = cir.alloca !s32i
// CIR:   cir.store %arg0, %[[CTX]]
// CIR:   cir.store {{.*}}, %[[X]]
// CIR:   cir.return

// LLVM: define internal void @__captured_stmt(ptr noundef %0)
// LLVM:   %[[CTXADDR:.*]] = alloca ptr
// LLVM:   %[[XADDR:.*]] = alloca i32
// LLVM:   store ptr %0, ptr %[[CTXADDR]]
// LLVM:   store i32 42, ptr %[[XADDR]]
// LLVM:   ret void

// CIR: cir.func {{.*}}@test_no_capture()
// CIR:   %[[AGG:.*]] = cir.alloca !rec_anon
// CIR:   cir.call @__captured_stmt(%[[AGG]])
// CIR:   cir.return

// LLVM: define dso_local void @test_no_capture()
// LLVM:   %[[AGG2:.*]] = alloca %struct.anon
// LLVM:   call void @__captured_stmt(ptr noundef %[[AGG2]])
// LLVM:   ret void
void test_no_capture(void) {
  #pragma clang __debug captured
  { int x = 42; }
}

// CIR: cir.func no_inline optnone internal private @__captured_stmt.1
// CIR:   %[[CTX2:.*]] = cir.alloca !cir.ptr<!rec_anon
// CIR:   cir.store %arg0, %[[CTX2]]
// CIR:   %[[CTXVAL:.*]] = cir.load {{.*}} %[[CTX2]]
// CIR:   cir.get_member %[[CTXVAL]][0]
// CIR:   cir.call @printf

// LLVM: define internal void @__captured_stmt.1(ptr noundef %0)
// LLVM:   store ptr %0, ptr %[[CTXADDR2:.*]]
// LLVM:   %[[CTXVAL2:.*]] = load ptr, ptr %[[CTXADDR2]]
// LLVM:   %[[FIELDPTR:.*]] = getelementptr inbounds nuw %struct.anon{{.*}}, ptr %[[CTXVAL2]], i32 0, i32 0
// LLVM:   %[[XPTR:.*]] = load ptr, ptr %[[FIELDPTR]]
// LLVM:   %[[XVAL:.*]] = load i32, ptr %[[XPTR]]
// LLVM:   %[[GVAL:.*]] = load i32, ptr @g
// LLVM:   call {{.*}}@printf

// CIR: cir.func {{.*}}@test_with_capture()
// CIR:   %[[XALLOC:.*]] = cir.alloca !s32i
// CIR:   %[[AGG3:.*]] = cir.alloca !rec_anon
// CIR:   cir.store {{.*}}, %[[XALLOC]]
// CIR:   cir.get_member %[[AGG3]][0]
// CIR:   cir.store {{.*}}%[[XALLOC]]
// CIR:   cir.call @__captured_stmt.1(%[[AGG3]])

// LLVM: define dso_local void @test_with_capture()
// LLVM:   %[[X3:.*]] = alloca i32
// LLVM:   %[[AGG4:.*]] = alloca %struct.anon
// LLVM:   store i32 42, ptr %[[X3]]
// LLVM:   %[[FPTR:.*]] = getelementptr inbounds nuw %struct.anon{{.*}}, ptr %[[AGG4]], i32 0, i32 0
// LLVM:   store ptr %[[X3]], ptr %[[FPTR]]
// LLVM:   call void @__captured_stmt.1(ptr noundef %[[AGG4]])
void test_with_capture(void) {
  int x = 42;
  #pragma clang __debug captured
  { printf("%d %d", x, g); }
}

// Test nested captured statements.
// The outer captured function captures 'a', the inner captures both 'a' and 'b'.
// CIR: cir.func no_inline optnone internal private @__captured_stmt.2
// CIR:   cir.alloca !cir.ptr<!rec_anon
// CIR:   cir.alloca !s32i
// CIR:   cir.alloca !rec_anon
// CIR:   cir.call @__captured_stmt.3

// CIR: cir.func no_inline optnone internal private @__captured_stmt.3
// CIR:   cir.alloca !cir.ptr<!rec_anon
// CIR:   cir.store %arg0
// CIR:   cir.load
// CIR:   cir.get_member {{.*}}[1]
// CIR:   cir.get_member {{.*}}[0]

// LLVM: define internal void @__captured_stmt.2(ptr noundef %0)
// LLVM:   alloca ptr
// LLVM:   alloca i32
// LLVM:   alloca %struct.anon
// LLVM:   call void @__captured_stmt.3(ptr noundef

// LLVM: define internal void @__captured_stmt.3(ptr noundef %0)
// LLVM:   store ptr %0, ptr %[[CTX5:.*]]
// LLVM:   %[[CTX5V:.*]] = load ptr, ptr %[[CTX5]]
// LLVM:   getelementptr inbounds nuw %struct.anon{{.*}}, ptr %[[CTX5V]], i32 0, i32 1
// LLVM:   getelementptr inbounds nuw %struct.anon{{.*}}, ptr %[[CTX5V]], i32 0, i32 0

// CIR: cir.func {{.*}}@test_nested()
// CIR:   %[[AALLOC:.*]] = cir.alloca !s32i
// CIR:   %[[AGG5:.*]] = cir.alloca !rec_anon
// CIR:   cir.get_member %[[AGG5]][0]
// CIR:   cir.store {{.*}}%[[AALLOC]]
// CIR:   cir.call @__captured_stmt.2(%[[AGG5]])

// LLVM: define dso_local void @test_nested()
// LLVM:   %[[A2:.*]] = alloca i32
// LLVM:   %[[AGG6:.*]] = alloca %struct.anon
// LLVM:   store i32 1, ptr %[[A2]]
// LLVM:   %[[FP2:.*]] = getelementptr inbounds nuw %struct.anon{{.*}}, ptr %[[AGG6]], i32 0, i32 0
// LLVM:   store ptr %[[A2]], ptr %[[FP2]]
// LLVM:   call void @__captured_stmt.2(ptr noundef %[[AGG6]])
void test_nested(void) {
  int a = 1;
  #pragma clang __debug captured
  {
    int b = 2;
    #pragma clang __debug captured
    { a = b; }
  }
}
