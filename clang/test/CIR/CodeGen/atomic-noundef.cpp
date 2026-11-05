// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that _Atomic parameters and return values do NOT get noundef,
// matching OGCG which excludes AtomicType from DetermineNoUndef via
// isScalarType() returning false.

void use_atomic(_Atomic int x);
void use_normal(int x);
_Atomic int ret_atomic();
int ret_normal();

// CHECK: define {{.*}} @_Z11test_paramsv()
// OGCG: define {{.*}} @_Z11test_paramsv()
void test_params() {
  // _Atomic int parameter should NOT have noundef.
  // CHECK: call void @{{.*}}use_atomic{{.*}}(i32 42){{$}}
  // OGCG: call void @{{.*}}use_atomic{{.*}}(i32 42){{$}}
  use_atomic(42);

  // Regular int parameter should have noundef.
  // CHECK: call void @_Z10use_normali(i32 noundef 42)
  // OGCG: call void @_Z10use_normali(i32 noundef 42)
  use_normal(42);
}

void test_returns() {
  // CHECK: call i32 @{{.*}}ret_atomic{{.*}}()
  // OGCG: call i32 @{{.*}}ret_atomic{{.*}}()
  ret_atomic();

  // CHECK: call noundef i32 @_Z10ret_normalv()
  // OGCG: call noundef i32 @_Z10ret_normalv()
  ret_normal();
}

// _Atomic return declaration should NOT have noundef on return.
// CHECK-DAG: declare i32 @{{.*}}ret_atomic{{.*}}()
// OGCG-DAG: declare i32 @{{.*}}ret_atomic{{.*}}()

// Regular return declaration should have noundef on return.
// CHECK-DAG: declare noundef i32 @_Z10ret_normalv()
// OGCG-DAG: declare noundef i32 @_Z10ret_normalv()
