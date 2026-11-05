// RUN: %clang_cc1 -fobjc-arc -triple x86_64-apple-darwin -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -fobjc-arc -triple x86_64-apple-darwin -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck %s --input-file=%t.cir.ll --check-prefix=CIR
// RUN: %clang_cc1 -fobjc-arc -triple x86_64-apple-darwin -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck %s --input-file=%t.ogcg.ll --check-prefix=OGCG

@interface NSMutableArray
- (id)objectAtIndexedSubscript:(int)index;
- (void)setObject:(id)object atIndexedSubscript:(int)index;
@end

id func(void) {
  NSMutableArray *array;
  array[3] = 0;
  return array[3];
}

// CIR-LABEL: define ptr @func()
// CIR: [[CALL:%.*]] = call ptr @objc_msgSend
// CIR: [[RET:%.*]] = call ptr @llvm.objc.retainAutoreleasedReturnValue(ptr [[CALL]])
// CIR-NOT: call ptr @llvm.objc.retain(
// CIR-NOT: call void @llvm.objc.release(
// CIR: call void @llvm.objc.storeStrong(ptr {{.*}}, ptr null)
// CIR: [[AR:%.*]] = call ptr @llvm.objc.autoreleaseReturnValue(ptr [[RET]])
// CIR: ret ptr [[AR]]

// OGCG-LABEL: define ptr @func()
// OGCG: [[CALL:%.*]] = call ptr @objc_msgSend
// OGCG: [[RET:%.*]] = {{.*}}call ptr @llvm.objc.retainAutoreleasedReturnValue(ptr [[CALL]])
// OGCG-NOT: call ptr @llvm.objc.retain(
// OGCG-NOT: call void @llvm.objc.release(
// OGCG: call void @llvm.objc.storeStrong(ptr {{.*}}, ptr null)
// OGCG: [[AR:%.*]] = {{.*}}call ptr @llvm.objc.autoreleaseReturnValue(ptr [[RET]])
// OGCG: ret ptr [[AR]]
