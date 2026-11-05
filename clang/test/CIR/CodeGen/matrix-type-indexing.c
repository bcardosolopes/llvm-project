// RUN: %clang_cc1 -fenable-matrix -fmatrix-memory-layout=row-major -triple x86_64-unknown-linux-gnu -fclangir %s -emit-llvm -disable-llvm-passes -o - | FileCheck %s --check-prefixes=CHECK,ROW-CHECK
// RUN: %clang_cc1 -fenable-matrix -fmatrix-memory-layout=column-major -triple x86_64-unknown-linux-gnu -fclangir %s -emit-llvm -disable-llvm-passes -o - | FileCheck %s --check-prefixes=CHECK,COL-CHECK

typedef float fx2x3_t __attribute__((matrix_type(2, 3)));
float Out[6];
fx2x3_t gM;

void binaryOpMatrixSubscriptExpr(int index, fx2x3_t M) {
  const unsigned int COLS = 3;
  unsigned int row = index / COLS;
  unsigned int col = index % COLS;
  // CHECK-LABEL: binaryOpMatrixSubscriptExpr
  // CHECK: [[ROW64:%.*]] = zext i32 %{{.*}} to i64
  // CHECK: [[COL64:%.*]] = zext i32 %{{.*}} to i64
  // ROW-CHECK: [[ROWOFF:%.*]] = mul i64 [[ROW64]], 3
  // ROW-CHECK-NEXT: [[IDX:%.*]] = add i64 [[ROWOFF]], [[COL64]]
  // COL-CHECK: [[COLOFF:%.*]] = mul i64 [[COL64]], 2
  // COL-CHECK-NEXT: [[IDX:%.*]] = add i64 [[COLOFF]], [[ROW64]]
  // CHECK: [[MAT:%.*]] = load <6 x float>, ptr %{{.*}}, align 4
  // CHECK-NEXT: [[ELT:%.*]] = extractelement <6 x float> [[MAT]], i64 [[IDX]]
  // CHECK: store float [[ELT]], ptr %{{.*}}, align 4
  Out[index] = M[row][col];
}

float returnMatrixSubscriptExpr(int row, int col, fx2x3_t M) {
  // CHECK-LABEL: returnMatrixSubscriptExpr
  // CHECK: [[ROW64:%.*]] = sext i32 %{{.*}} to i64
  // CHECK: [[COL64:%.*]] = sext i32 %{{.*}} to i64
  // ROW-CHECK: [[ROWOFF:%.*]] = mul i64 [[ROW64]], 3
  // ROW-CHECK-NEXT: [[IDX:%.*]] = add i64 [[ROWOFF]], [[COL64]]
  // COL-CHECK: [[COLOFF:%.*]] = mul i64 [[COL64]], 2
  // COL-CHECK-NEXT: [[IDX:%.*]] = add i64 [[COLOFF]], [[ROW64]]
  // CHECK: [[MAT:%.*]] = load <6 x float>, ptr %{{.*}}, align 4
  // CHECK-NEXT: [[ELT:%.*]] = extractelement <6 x float> [[MAT]], i64 [[IDX]]
  // CHECK: ret float %{{.*}}
  return M[row][col];
}

void storeAtMatrixSubscriptExpr(int row, int col, float value) {
  // CHECK-LABEL: storeAtMatrixSubscriptExpr
  // CHECK: [[VAL:%.*]] = load float, ptr %{{.*}}, align 4
  // CHECK: [[ROW64:%.*]] = sext i32 %{{.*}} to i64
  // CHECK: [[COL64:%.*]] = sext i32 %{{.*}} to i64
  // ROW-CHECK: [[ROWOFF:%.*]] = mul i64 [[ROW64]], 3
  // ROW-CHECK-NEXT: [[IDX:%.*]] = add i64 [[ROWOFF]], [[COL64]]
  // COL-CHECK: [[COLOFF:%.*]] = mul i64 [[COL64]], 2
  // COL-CHECK-NEXT: [[IDX:%.*]] = add i64 [[COLOFF]], [[ROW64]]
  // CHECK-NEXT: [[MAT:%.*]] = load <6 x float>, ptr @gM, align 4
  // CHECK-NEXT: [[NEW:%.*]] = insertelement <6 x float> [[MAT]], float [[VAL]], i64 [[IDX]]
  // CHECK-NEXT: store <6 x float> [[NEW]], ptr @gM, align 4
  gM[row][col] = value;
}
