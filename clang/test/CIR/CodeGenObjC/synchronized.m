// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-exceptions -fclangir -o - %s | FileCheck %s --check-prefix=CHECK-MRR
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-exceptions -fclangir -o - %s | FileCheck %s --check-prefix=CHECK-ARC

id make(void);

void leave(id a) {
  @synchronized(a) {
    return;
  }
}

// CHECK-MRR-LABEL: define{{.*}} void @leave(
// CHECK-MRR: call i32 @objc_sync_enter(ptr %{{.*}})
// CHECK-MRR: call i32 @objc_sync_exit(ptr %{{.*}})
// CHECK-MRR-NEXT: ret void

void arc_leave(void) {
  @synchronized(make()) {
    return;
  }
}

// CHECK-ARC-LABEL: define{{.*}} void @arc_leave()
// CHECK-ARC: [[T0:%.*]] = call ptr @make()
// CHECK-ARC-NEXT: [[T1:%.*]] = {{.*}}call ptr @llvm.objc.retainAutoreleasedReturnValue(ptr [[T0]])
// CHECK-ARC: call i32 @objc_sync_enter(ptr [[T1]])
// CHECK-ARC: call i32 @objc_sync_exit(ptr %{{.*}})
// CHECK-ARC: call void @llvm.objc.release(ptr [[T1]])
// CHECK-ARC-NEXT: ret void
