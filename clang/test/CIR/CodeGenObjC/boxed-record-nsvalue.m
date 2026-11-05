// RUN: %clang_cc1 -I %S/../../CodeGenObjC/Inputs -triple x86_64-apple-macosx -emit-llvm -fobjc-arc -O2 -disable-llvm-passes -fclangir -o - %s | FileCheck %s

#import "nsvalue-boxed-expressions-support.h"

// CHECK: @[[ENC:[A-Za-z0-9_$.]+]] = private unnamed_addr constant [14 x i8] c"{_NSRange=QQ}\00"

void doRange(void) {
  NSRange ns_range = { .location = 0, .length = 42 };
  NSValue *range = @(ns_range);
  (void)range;
}

// CHECK-LABEL: define{{.*}} void @doRange()
// CHECK: alloca %struct._NSRange
// CHECK: call void @llvm.memcpy
// CHECK: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef @[[ENC]]) [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
// CHECK: call void (...) @llvm.objc.clang.arc.noop.use

NSRange getRange(void);

void doRangeRValue(void) {
  NSValue *range = @(getRange());
  (void)range;
}

// CHECK-LABEL: define{{.*}} void @doRangeRValue()
// CHECK: alloca %struct._NSRange
// CHECK: extractvalue
// CHECK: store i64
// CHECK: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef @[[ENC]]) [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
