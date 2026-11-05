// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -fobjc-arc -O0 \
// RUN:   -fobjc-runtime=ios -fclangir -emit-llvm %s -o - | FileCheck %s

// Regression test: at -O0 (no attached-call bundle) the
// objc_retainAutoreleasedReturnValue call must be preceded by the target's
// inline-asm return-value marker, matching classic codegen. The marker lets
// the runtime recognize the +1-return handshake and take its fast path.

__attribute__((objc_root_class)) @interface O @end
@implementation O @end

O *make(void);

void use(void) {
  O *f = make();
  (void)f;
}

// CHECK-LABEL: define void @use()
// CHECK:         call ptr @make()
// CHECK:         call void asm sideeffect "mov{{.*}}fp, fp{{.*}}marker for objc_retainAutoreleaseReturnValue"
// CHECK:         call ptr @llvm.objc.retainAutoreleasedReturnValue(
// CHECK:       }
