// RUN: %clang_cc1 -triple i686-windows-itanium -fms-extensions -fobjc-runtime=macosx -fdeclspec -fobjc-exceptions -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple i686-windows-itanium -fms-extensions -fobjc-runtime=macosx -fdeclspec -fobjc-exceptions -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

@interface I
+ (id)new;
@end

int g(void) {
  @autoreleasepool {
    @try {
      @throw @"boom";
    } @catch (id ex) {
      return 1;
    } @catch (I *ex) {
      return 2;
    }
  }
  return 0;
}

// CIR-LABEL: cir.func{{.*}} @g()
// CIR: cir.try {
// CIR:   cir.call @objc_exception_throw
// CIR: } catch [type #cir.global_view<@OBJC_EHTYPE_id> : !cir.ptr<!void>] {
// CIR:   cir.catch_param : !cir.ptr<!void> {cir.catch_begin_fn = "objc_begin_catch", cir.catch_end_fn = "objc_end_catch", cir.end_catch_nothrow}
// CIR:   cir.catch_param end {cir.catch_end_fn = "objc_end_catch", cir.end_catch_nothrow}
// CIR: } catch [type #cir.global_view<@OBJC_EHTYPE_$_I> : !cir.ptr<!void>] {
// CIR:   cir.catch_param : !cir.ptr<!void> {cir.catch_begin_fn = "objc_begin_catch", cir.catch_end_fn = "objc_end_catch", cir.end_catch_nothrow}
// CIR:   cir.catch_param end {cir.catch_end_fn = "objc_end_catch", cir.end_catch_nothrow}

// LLVM-LABEL: define dso_local i32 @g()
// LLVM-NOT: personality
// LLVM-NOT: landingpad
// LLVM: call void @objc_exception_throw
// LLVM: {{[0-9]+}}:{{.*}}No predecessors!
// LLVM: call ptr @objc_begin_catch(ptr undef) #4
// LLVM: br label %[[END_CATCH:.*]]
// LLVM: {{[0-9]+}}:{{.*}}No predecessors!
// LLVM: call ptr @objc_begin_catch(ptr undef) #4
// LLVM: br label %[[END_CATCH]]
// Both handlers leave the catch the same way, so cir-tail-merge gives them one
// shared exit block instead of a copy each.
// LLVM: [[END_CATCH]]:
// LLVM: call void @objc_end_catch() #4
