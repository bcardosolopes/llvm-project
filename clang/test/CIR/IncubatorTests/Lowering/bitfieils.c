// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

typedef struct {
  int a : 4;
} B;

// LLVM: define dso_local void @set_signed
// LLVM:   [[TMP0:%.*]] = load ptr
// LLVM:   [[TMP1:%.*]] = getelementptr inbounds nuw %struct.B, ptr [[TMP0]], i32 0, i32 0
// LLVM:   [[TMP2:%.*]] = load i8, ptr [[TMP1]]
// LLVM:   [[TMP3:%.*]] = and i8 [[TMP2]], -16
// LLVM:   [[TMP4:%.*]] = or i8 [[TMP3]], 14
// LLVM:   store i8 [[TMP4]], ptr [[TMP1]]
void set_signed(B* b) {
  b->a = -2;
}

// LLVM: define dso_local i32 @get_signed
// LLVM:   [[TMP0:%.*]] = load ptr
// LLVM:   [[TMP1:%.*]] = getelementptr inbounds nuw %struct.B, ptr [[TMP0]], i32 0, i32 0
// LLVM:   [[TMP2:%.*]] = load i8, ptr [[TMP1]]
// LLVM:   [[TMP3:%.*]] = shl i8 [[TMP2]], 4
// LLVM:   [[TMP4:%.*]] = ashr i8 [[TMP3]], 4
// LLVM:   [[TMP5:%.*]] = sext i8 [[TMP4]] to i32
// LLVM:   ret i32 [[TMP5]]
int get_signed(B* b) {
  return b->a;
}
