// RUN: %clang_cc1 -fclangir -emit-cir -fobjc-arc -fobjc-runtime=ios-14.0 \
// RUN:   %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -fclangir -emit-llvm -fobjc-arc -fobjc-runtime=ios-14.0 \
// RUN:   %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// Test that ObjC ARC code emits proper runtime calls for retain/release.

id test(void) {
  id obj = (id)0;
  return obj;
}

// CIR: cir.call @objc_retain
// CIR: cir.call @objc_storeStrong
// LLVM: call {{.*}} @llvm.objc.retain
// LLVM: call void @llvm.objc.storeStrong
