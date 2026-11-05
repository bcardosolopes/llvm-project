// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that callsite nounwind attributes are emitted for calls to
// noexcept/nothrow functions, including declaration-only functions.

void noexcept_decl() noexcept;
void noexcept_def() noexcept {}
void regular_decl();
__attribute__((nothrow)) void nothrow_decl();

// CHECK: define {{.*}} @_Z9test_declv()
// OGCG: define {{.*}} @_Z9test_declv()
void test_decl() {
  // Declaration-only noexcept function call must have nounwind on the callsite.
  // CHECK: call void @_Z13noexcept_declv() #[[NW:[0-9]+]]
  // OGCG: call void @_Z13noexcept_declv() #[[NW:[0-9]+]]
  noexcept_decl();
}

// CHECK: define {{.*}} @_Z8test_defv()
// OGCG: define {{.*}} @_Z8test_defv()
void test_def() {
  // Defined noexcept function call must have nounwind on the callsite.
  // CHECK: call void @_Z12noexcept_defv() #[[NW]]
  // OGCG: call void @_Z12noexcept_defv() #[[NW]]
  noexcept_def();
}

// CHECK: define {{.*}} @_Z12test_regularv()
// OGCG: define {{.*}} @_Z12test_regularv()
void test_regular() {
  // Regular function call should NOT have nounwind on the callsite.
  // CHECK: call void @_Z12regular_declv(){{$}}
  // OGCG: call void @_Z12regular_declv(){{$}}
  regular_decl();
}

// CHECK: define {{.*}} @_Z12test_nothrowv()
// OGCG: define {{.*}} @_Z12test_nothrowv()
void test_nothrow() {
  // __attribute__((nothrow)) function call must have nounwind on the callsite.
  // CHECK: call void @_Z12nothrow_declv() #[[NW]]
  // OGCG: call void @_Z12nothrow_declv() #[[NW]]
  nothrow_decl();
}

// CHECK: attributes #[[NW]] = { nounwind }
// OGCG: attributes #[[NW]] = { nounwind }
