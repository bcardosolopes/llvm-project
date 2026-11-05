// RUN: %clang_cc1 -triple arm64_32-apple-ios7.0 -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple arm64_32-apple-ios7.0 -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

struct DefaultPtr {
  int *x;
};

void take_default(DefaultPtr s) { *s.x = 1; }

// LLVM-LABEL: define void @_Z12copy_defaultv(
// LLVM: call void @llvm.memcpy.p0.p0.i32(ptr align 4 {{.*}}, ptr align 4 {{.*}}, i32 4, i1 false)
// OGCG-LABEL: define void @_Z12copy_defaultv(
// OGCG: call void @llvm.memcpy.p0.p0.i32(ptr align 4 {{.*}}, ptr align 4 {{.*}}, i32 4, i1 false)
void copy_default() {
  DefaultPtr s;
  take_default(s);
}

struct TargetPtr {
  __attribute__((address_space(100))) int *x;
};

void take_target(TargetPtr s) { *s.x = 1; }

// LLVM-LABEL: define void @_Z11copy_targetv(
// LLVM: call void @llvm.memcpy.p0.p0.i32(ptr align 4 {{.*}}, ptr align 4 {{.*}}, i32 4, i1 false)
// OGCG-LABEL: define void @_Z11copy_targetv(
// OGCG: call void @llvm.memcpy.p0.p0.i32(ptr align 4 {{.*}}, ptr align 4 {{.*}}, i32 4, i1 false)
void copy_target() {
  TargetPtr s;
  take_target(s);
}

struct TargetPtrArray {
  __attribute__((address_space(100))) int *x[2];
};

void take_target_array(TargetPtrArray s) { *s.x[0] = 1; }

// LLVM-LABEL: define void @_Z17copy_target_arrayv(
// LLVM: call void @llvm.memcpy.p0.p0.i32(ptr align 4 {{.*}}, ptr align 4 {{.*}}, i32 8, i1 false)
// OGCG-LABEL: define void @_Z17copy_target_arrayv(
// OGCG: call void @llvm.memcpy.p0.p0.i32(ptr align 4 {{.*}}, ptr align 4 {{.*}}, i32 8, i1 false)
void copy_target_array() {
  TargetPtrArray s;
  take_target_array(s);
}
