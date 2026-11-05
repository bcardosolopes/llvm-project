// RUN: %clang_cc1 %s -fptrauth-function-pointer-type-discrimination -triple arm64-apple-ios13 -fptrauth-calls -fptrauth-intrinsics -disable-llvm-passes -fclangir -emit-llvm -o - | FileCheck %s --check-prefix=TYPE
// RUN: %clang_cc1 %s -triple arm64-apple-ios13 -fptrauth-calls -fptrauth-intrinsics -disable-llvm-passes -fclangir -emit-llvm -o - | FileCheck %s --check-prefix=ZERO
// RUN: %clang_cc1 -xc++ %s -fptrauth-function-pointer-type-discrimination -triple arm64-apple-ios13 -fptrauth-calls -fptrauth-intrinsics -disable-llvm-passes -fclangir -emit-llvm -o - | FileCheck %s --check-prefix=CXX-TYPE

void (*fptr)(void);
void (* __ptrauth(0, 0, 42) f2ptr_42_discm)(int);
void f(int);
void (* const __ptrauth(0, 0, 42) f_const_ptr)(int) = &f;

// TYPE: @f_const_ptr = constant ptr ptrauth (ptr @f, i32 0, i64 42), align 8
// ZERO: @f_const_ptr = constant ptr ptrauth (ptr @f, i32 0, i64 42), align 8

void test_const_ptr_function_call(void) {
  f_const_ptr(1);
  // TYPE: call void ptrauth (ptr @f, i32 0, i64 2712)(i32 noundef 1) [ "ptrauth"(i32 0, i64 2712) ]
  // ZERO: call void ptrauth (ptr @f, i32 0)(i32 noundef 1) [ "ptrauth"(i32 0, i64 0) ]
}

#ifdef __cplusplus
void (* get_fptr(void))(int);
void (* __ptrauth(0, 0, 42) f_const_ptr2)(int) = get_fptr();
void (* const __ptrauth(0, 1, 43) &f_ref)(int) = f_const_ptr2;

// CXX-TYPE-LABEL: define internal void @__cxx_global_var_init.1()
// CXX-TYPE: %[[LOAD:.*]] = load ptr, ptr @f_const_ptr2, align 8
// CXX-TYPE: %[[BLEND:.*]] = call i64 @llvm.ptrauth.blend(i64 ptrtoint (ptr @_ZGR5f_ref_ to i64), i64 43)
// CXX-TYPE: %[[LOADINT:.*]] = ptrtoint ptr %[[LOAD]] to i64
// CXX-TYPE: %[[RESIGN:.*]] = call i64 @llvm.ptrauth.resign(i64 %[[LOADINT]], i32 0, i64 42, i32 0, i64 %[[BLEND]])
// CXX-TYPE-NOT: i64 2712
// CXX-TYPE: store ptr %{{.*}}, ptr @_ZGR5f_ref_, align 8

void test_const_ptr_ref_function_call(void) {
  f_ref(1);
  // CXX-TYPE: %[[REF:.*]] = load ptr, ptr @f_ref, align 8
  // CXX-TYPE: %[[CALLEE:.*]] = load ptr, ptr %[[REF]], align 8
  // CXX-TYPE: %[[REFINT:.*]] = ptrtoint ptr %[[REF]] to i64
  // CXX-TYPE: %[[DISC:.*]] = call i64 @llvm.ptrauth.blend(i64 %[[REFINT]], i64 43)
  // CXX-TYPE-NOT: llvm.ptrauth.resign
  // CXX-TYPE: call void %[[CALLEE]](i32 noundef 1) [ "ptrauth"(i32 0, i64 %[[DISC]]) ]
}
#endif
