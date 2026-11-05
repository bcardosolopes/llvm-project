// -fextend-variable-liveness emits llvm.fake.use over live variables. CIRGen
// must lower these to the recognized `llvm.fake.use` INTRINSIC (via the
// llvm-intrinsic call path), not to an ordinary external function -- otherwise
// the call survives to link time as an undefined `llvm.fake.use` symbol once a
// definition is merged/LTO'd across translation units, and the backend's
// fake-use eliminator (which gates on Intrinsic::fake_use) never fires.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O0 -fclangir \
// RUN:   -fextend-variable-liveness=all -emit-llvm %s -o - | FileCheck %s

// The call is the varargs intrinsic form.
// CHECK-LABEL: define{{.*}} i32 @_Z3usei
// CHECK: call void (...) @llvm.fake.use(
int use(int x) {
  int local = x + 1;
  return local;
}

// The declaration must be the recognized intrinsic: varargs, void return, with
// the intrinsic attribute group. LLVM only prints the "Function Attrs" comment
// with nocallback/inaccessiblemem for a declaration it recognizes as the
// llvm.fake.use intrinsic; an ordinary external function would not carry them.
// CHECK: ; Function Attrs: nocallback nofree nosync nounwind willreturn memory(inaccessiblemem: readwrite)
// CHECK-NEXT: declare void @llvm.fake.use(...)
