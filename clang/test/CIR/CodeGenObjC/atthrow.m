// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-exceptions -fclangir -o - %s | FileCheck %s --check-prefix=CHECK-MRR
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fobjc-exceptions -fclangir -o - %s | FileCheck %s --check-prefix=CHECK-ARC

id make(void);

void throw_value(id x) {
  @throw x;
}

void throw_call(void) {
  @throw make();
}

// CHECK-MRR-LABEL: define{{.*}} void @throw_value
// CHECK-MRR: call void @objc_exception_throw(ptr %{{.*}}) [[NR:#[0-9]+]]
// CHECK-MRR-NEXT: unreachable
// CHECK-MRR: attributes [[NR]] = { noreturn }

// CHECK-ARC-LABEL: define{{.*}} void @throw_call()
// CHECK-ARC: [[T0:%.*]] = call ptr @make()
// CHECK-ARC-NEXT: [[T1:%.*]] = {{.*}}call ptr @llvm.objc.retainAutoreleasedReturnValue(ptr [[T0]])
// CHECK-ARC-NEXT: [[T2:%.*]] = call ptr @llvm.objc.autorelease(ptr [[T1]])
// CHECK-ARC-NEXT: call void @objc_exception_throw(ptr [[T2]]) [[ARCNR:#[0-9]+]]
// CHECK-ARC-NEXT: unreachable
// CHECK-ARC: attributes [[ARCNR]] = { noreturn }
