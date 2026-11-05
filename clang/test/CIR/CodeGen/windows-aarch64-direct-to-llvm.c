// RUN: %clang_cc1 -triple aarch64-windows-msvc -x c -fclangir -emit-llvm %s -o - | FileCheck %s

typedef struct {
  short a;
  int b;
  short c;
} S;

S getS(void);

void f(void) {
  S s = getS();
}

// CHECK: %struct.S = type { i16, i32, i16 }
// CHECK-LABEL: define dso_local void @f()
// CHECK: %[[TMP:.+]] = alloca [2 x i64], i64 1, align 8
// CHECK: %[[SLOT:.+]] = alloca %struct.S, i64 1, align 4
// CHECK: %[[VAL:.+]] = call [2 x i64] @getS()
// CHECK: store [2 x i64] %[[VAL]], ptr %[[TMP]], align 8
// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 4 %[[SLOT]], ptr align 8 %[[TMP]], i64 12, i1 false)
// CHECK: ret void
