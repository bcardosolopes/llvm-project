// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

typedef __builtin_va_list va_list;
#define va_start(ap, param) __builtin_va_start(ap, param)

typedef struct {
  struct {} a;
  double b;
} s1;

s1 test_s1(int z, ...) {
  va_list list;
  va_start(list, z);
  return __builtin_va_arg(list, s1);
}

// LLVM-LABEL: define dso_local double @test_s1(
// LLVM: [[RETVAL:%.*]] = alloca %struct.s1{{.*}}align 8
// LLVM-NOT: alloca %struct.s1{{.*}}align 8
// LLVM: [[VAARG_ADDR:%.*]] = phi ptr
// LLVM-NOT: load %struct.s1, ptr [[VAARG_ADDR]], align 8
// LLVM-NOT: store %struct.s1
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 8 [[RETVAL]], ptr align 8 [[VAARG_ADDR]], i64 8, i1 false)
