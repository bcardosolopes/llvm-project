// RUN: %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics -std=c++11 -emit-llvm %s -o - | FileCheck -check-prefixes=CHECK,OGCG %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fptrauth-calls -fptrauth-intrinsics -std=c++11 -emit-llvm %s -o - | FileCheck -check-prefixes=CHECK,OGCG %s
// RUN: %if cir-support %{ %clang_cc1 -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics -std=c++11 -emit-llvm -fclangir %s -o - | FileCheck -check-prefixes=CHECK,CIR %s %}
// RUN: %if cir-support %{ %clang_cc1 -triple aarch64-linux-gnu -fptrauth-calls -fptrauth-intrinsics -std=c++11 -emit-llvm -fclangir %s -o - | FileCheck -check-prefixes=CHECK,CIR %s %}

#define AQ __ptrauth(1,1,50)
#define IQ __ptrauth(1,0,50)

// CHECK: %[[STRUCT_SA:.*]] = type { ptr, ptr }
// CHECK: %[[STRUCT_SI:.*]] = type { ptr }
// CHECK: define {{.*}}void @_Z18testParamTrivialSA9TrivialSA(ptr dead_on_return noundef %{{.*}})

// OGCG-LABEL: define {{.*}}void @_Z19testCopyConstructor2SA(ptr
// OGCG: call {{.*}}@_ZN2SAC1ERKS_(
// OGCG-LABEL: define {{.*}}@_ZN2SAC1ERKS_(
// OGCG: call {{.*}}@_ZN2SAC2ERKS_(
// OGCG-LABEL: define {{.*}}void @_Z19testMoveConstructor2SA(ptr
// OGCG: call {{.*}}@_ZN2SAC1EOS_(
// OGCG-LABEL: define {{.*}}@_ZN2SAC1EOS_(
// OGCG: call {{.*}}@_ZN2SAC2EOS_(
// OGCG-LABEL: define {{.*}}void @_Z18testCopyAssignment2SA(ptr
// OGCG: call {{.*}}@_ZN2SAaSERKS_(
// OGCG-LABEL: define {{.*}}@_ZN2SAaSERKS_(
// OGCG-NOT: @llvm.ptrauth.auth(
// OGCG-NOT: @llvm.ptrauth.sign(
// OGCG: @llvm.ptrauth.resign(
// OGCG-LABEL: define {{.*}}void @_Z18testMoveAssignment2SA(ptr
// OGCG: call {{.*}}@_ZN2SAaSEOS_(
// OGCG-LABEL: define {{.*}}@_ZN2SAaSEOS_(
// OGCG-NOT: @llvm.ptrauth.auth(
// OGCG-NOT: @llvm.ptrauth.sign(
// OGCG: @llvm.ptrauth.resign(
// OGCG-LABEL: define {{.*}}void @_Z19testCopyConstructor2SI(
// OGCG: call void @llvm.memcpy.p0.p0.i64(
// OGCG-LABEL: define {{.*}}void @_Z19testMoveConstructor2SI(
// OGCG: call void @llvm.memcpy.p0.p0.i64(
// OGCG-LABEL: define {{.*}}void @_Z18testCopyAssignment2SI(
// OGCG: call void @llvm.memcpy.p0.p0.i64(
// OGCG-LABEL: define {{.*}}void @_Z18testMoveAssignment2SI(
// OGCG: call void @llvm.memcpy.p0.p0.i64(
// OGCG-LABEL: define {{.*}}@_ZN2SAC2ERKS_(
// OGCG-NOT: @llvm.ptrauth.auth(
// OGCG-NOT: @llvm.ptrauth.sign(
// OGCG: @llvm.ptrauth.resign(
// OGCG-LABEL: define {{.*}}@_ZN2SAC2EOS_(
// OGCG-NOT: @llvm.ptrauth.auth(
// OGCG-NOT: @llvm.ptrauth.sign(
// OGCG: @llvm.ptrauth.resign(

// CIR-LABEL: define {{.*}}@_ZN2SAC2ERKS_(
// CIR-NOT: @llvm.ptrauth.auth(
// CIR-NOT: @llvm.ptrauth.sign(
// CIR: @llvm.ptrauth.resign(
// CIR-LABEL: define {{.*}}@_ZN2SAC1ERKS_(
// CIR: call {{.*}}@_ZN2SAC2ERKS_(
// CIR-LABEL: define {{.*}}void @_Z19testCopyConstructor2SA(ptr
// CIR: call {{.*}}@_ZN2SAC1ERKS_(
// CIR-LABEL: define {{.*}}@_ZN2SAC2EOS_(
// CIR-NOT: @llvm.ptrauth.auth(
// CIR-NOT: @llvm.ptrauth.sign(
// CIR: @llvm.ptrauth.resign(
// CIR-LABEL: define {{.*}}@_ZN2SAC1EOS_(
// CIR: call {{.*}}@_ZN2SAC2EOS_(
// CIR-LABEL: define {{.*}}void @_Z19testMoveConstructor2SA(ptr
// CIR: call {{.*}}@_ZN2SAC1EOS_(
// CIR-LABEL: define {{.*}}@_ZN2SAaSERKS_(
// CIR-NOT: @llvm.ptrauth.auth(
// CIR-NOT: @llvm.ptrauth.sign(
// CIR: @llvm.ptrauth.resign(
// CIR-LABEL: define {{.*}}void @_Z18testCopyAssignment2SA(ptr
// CIR: call {{.*}}@_ZN2SAaSERKS_(
// CIR-LABEL: define {{.*}}@_ZN2SAaSEOS_(
// CIR-NOT: @llvm.ptrauth.auth(
// CIR-NOT: @llvm.ptrauth.sign(
// CIR: @llvm.ptrauth.resign(
// CIR-LABEL: define {{.*}}void @_Z18testMoveAssignment2SA(ptr
// CIR: call {{.*}}@_ZN2SAaSEOS_(
// CIR-LABEL: define {{.*}}void @_Z19testCopyConstructor2SI(
// CIR: call void @llvm.memcpy.p0.p0.i64(
// CIR-LABEL: define {{.*}}void @_Z19testMoveConstructor2SI(
// CIR: call void @llvm.memcpy.p0.p0.i64(
// CIR-LABEL: define {{.*}}void @_Z18testCopyAssignment2SI(
// CIR: call void @llvm.memcpy.p0.p0.i64(
// CIR-LABEL: define {{.*}}void @_Z18testMoveAssignment2SI(
// CIR: call void @llvm.memcpy.p0.p0.i64(

struct SA {
  int * AQ m0; // Signed using address discrimination.
  int * AQ m1; // Signed using address discrimination.
};

struct SI {
  int * IQ m; // No address discrimination.
};

struct __attribute__((trivial_abi)) TrivialSA {
  int * AQ m0; // Signed using address discrimination.
  int * AQ m1; // Signed using address discrimination.
};

void testParamTrivialSA(TrivialSA a) {}

void testCopyConstructor(SA a) {
  SA t = a;
}

void testMoveConstructor(SA a) {
  SA t = static_cast<SA &&>(a);
}

void testCopyAssignment(SA a) {
  SA t;
  t = a;
}

void testMoveAssignment(SA a) {
  SA t;
  t = static_cast<SA &&>(a);
}

void testCopyConstructor(SI a) {
  SI t = a;
}

void testMoveConstructor(SI a) {
  SI t = static_cast<SI &&>(a);
}

void testCopyAssignment(SI a) {
  SI t;
  t = a;
}

void testMoveAssignment(SI a) {
  SI t;
  t = static_cast<SI &&>(a);
}
