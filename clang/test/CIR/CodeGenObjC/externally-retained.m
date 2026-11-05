// RUN: %clang_cc1 -triple x86_64-apple-macosx10.13.0 -fobjc-arc -fblocks -Wno-objc-root-class -O0 -disable-llvm-passes -emit-llvm -fclangir -o - %s | FileCheck %s

#define EXT_RET __attribute__((objc_externally_retained))

@interface ObjTy @end

ObjTy *global;

void param(ObjTy *p) EXT_RET {
}

// CHECK-LABEL: define{{.*}} void @param(
// CHECK-NOT: llvm.objc.
// CHECK: ret void

void local(void) {
  EXT_RET ObjTy *local = global;
}

// CHECK-LABEL: define{{.*}} void @local()
// CHECK: %[[LOCAL:.*]] = alloca ptr
// CHECK-NEXT: %[[GLOBAL:.*]] = load ptr, ptr @global
// CHECK-NEXT: store ptr %[[GLOBAL]], ptr %[[LOCAL]]
// CHECK-NOT: llvm.objc.
// CHECK: ret void

void in_init(void) {
  EXT_RET ObjTy *wat = 0 ? wat : global;
}

// CHECK-LABEL: define{{.*}} void @in_init()
// CHECK: %[[WAT:.*]] = alloca ptr
// CHECK-NEXT: store ptr null, ptr %[[WAT]]
// CHECK-NEXT: %[[GLOBAL:.*]] = load ptr, ptr @global
// CHECK-NEXT: %[[OLD:.*]] = load ptr, ptr %[[WAT]]
// CHECK-NEXT: store ptr %[[GLOBAL]], ptr %[[WAT]]
// CHECK-NEXT: call void @llvm.objc.release(ptr %[[OLD]])
// CHECK-NOT: llvm.objc.retain
// CHECK: ret void
