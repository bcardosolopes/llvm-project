// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm \
// RUN:     -std=c++20 -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -emit-llvm \
// RUN:     -std=c++20 -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Test that AArch64 struct argument coercion handles direct RecordType values
// (e.g., undef constants, call returns) that are not produced by a LoadOp.
// Previously, enterRecordPointerForCoercedAccess asserted on PointerType
// when the struct value was not traceable to a pointer via LoadOp.

struct S { S(); };
void Call(S);

void test_undef_struct_arg() {
  S s;
  Call(s);
}

// Both CIR and OGCG should coerce struct S (1-byte padded struct) to i64
// for the AArch64 calling convention.

// LLVM-LABEL: define {{.*}} void @_Z21test_undef_struct_argv()
// LLVM: call void @_ZN1SC1Ev(
// LLVM: call void @_Z4Call1S(i64 %{{.*}})

// OGCG-LABEL: define {{.*}} void @_Z21test_undef_struct_argv()
// OGCG: call void @_ZN1SC1Ev(
// OGCG: call void @_Z4Call1S(i64 %{{.*}})

// Also test with a struct returned from a call, which produces a direct
// RecordType SSA value (not from LoadOp).
struct R { int x; };
R make();
void consume(R);

void test_call_return_struct_arg() {
  consume(make());
}

// LLVM-LABEL: define {{.*}} void @_Z27test_call_return_struct_argv()
// LLVM: call i32 @_Z4makev()
// LLVM: call void @_Z7consume1R(i64 %{{.*}})

// OGCG-LABEL: define {{.*}} void @_Z27test_call_return_struct_argv()
// OGCG: call i32 @_Z4makev()
// OGCG: call void @_Z7consume1R(i64 %{{.*}})
