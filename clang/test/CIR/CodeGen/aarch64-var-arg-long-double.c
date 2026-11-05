// RUN: %clang_cc1 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Test that va_arg with long double (fp128 on AArch64) is correctly lowered
// through the VR register path instead of crashing with an unsupported type
// assertion.

long double f_longdouble(int n, ...) {
  __builtin_va_list valist;
  __builtin_va_start(valist, n);
  long double res = __builtin_va_arg(valist, long double);
  __builtin_va_end(valist);
  return res;
}

// Verify long double uses the VR (floating-point) register path.
// LLVM: define dso_local fp128 @f_longdouble(i32 noundef %0, ...)
// LLVM: [[VR_OFFS_P:%.*]] = getelementptr inbounds nuw %struct.__va_list, ptr {{%.*}}, i32 0, i32 4
// LLVM: [[VR_OFFS:%.*]] = load i32, ptr [[VR_OFFS_P]]
// LLVM: add i32 [[VR_OFFS]], 16
// LLVM: getelementptr inbounds nuw %struct.__va_list, ptr {{%.*}}, i32 0, i32 2
// LLVM: getelementptr inbounds nuw %struct.__va_list, ptr {{%.*}}, i32 0, i32 0
// LLVM: getelementptr inbounds i8, ptr {{%.*}}, i64 15
// LLVM: call ptr @llvm.ptrmask.p0.i64(ptr {{%.*}}, i64 -16)
// LLVM: getelementptr inbounds i8, ptr {{%.*}}, i64 16
// LLVM: load fp128, ptr
// LLVM: ret fp128
