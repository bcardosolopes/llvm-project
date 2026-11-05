// RUN: %clang_cc1 -triple i386-apple-darwin10 -fobjc-runtime=macosx-fragile-10.5 -emit-llvm -fobjc-exceptions -fclangir -o - %s | FileCheck %s --check-prefix=MRR
// RUN: %clang_cc1 -triple i386-apple-darwin10 -fobjc-runtime=macosx-fragile-10.10 -emit-llvm -fobjc-arc -fobjc-exceptions -fclangir -o - %s | FileCheck %s --check-prefix=ARC

@class A;
extern void helper(void);
extern void cleanup(void);
extern void checkpoint(int);

void fragile_rethrow(void) {
  @try {
    helper();
  } @catch (id e) {
    @throw;
  }
}

// MRR-LABEL: define void @fragile_rethrow()
// MRR: [[CATCH:%.*]] = alloca ptr, i64 1, align 4
// MRR: [[EXN:%.*]] = alloca { [18 x i32], [4 x ptr] }, i64 1, align 4
// MRR: [[BUF:%.*]] = getelementptr inbounds nuw { [18 x i32], [4 x ptr] }, ptr [[EXN]], i32 0, i32 0
// MRR: call void @objc_exception_try_enter(ptr [[EXN]])
// MRR: call i32 @_setjmp(ptr [[BUF]]) #[[SETJMP_CALL:[0-9]+]]
// MRR: call void asm sideeffect "", "*m{{.*}}"(ptr elementtype({ [18 x i32], [4 x ptr] }) [[EXN]])
// MRR: call void @helper()
// MRR: call void @objc_exception_try_exit(ptr [[EXN]])
// MRR: call void asm sideeffect "", "=*m{{.*}}"(ptr elementtype({ [18 x i32], [4 x ptr] }) [[EXN]])
// MRR: [[CAUGHT:%.*]] = call ptr @objc_exception_extract(ptr [[EXN]])
// MRR: store ptr [[CAUGHT]], ptr [[CATCH]], align 4
// MRR: call void @objc_exception_throw(ptr [[CAUGHT]]) #[[THROW_CALL:[0-9]+]]
// MRR: unreachable

void fragile_catch_finally(void) {
  @try {
    helper();
  } @catch (A *x) {
    checkpoint(1);
  } @finally {
    cleanup();
  }
}

// MRR-LABEL: define void @fragile_catch_finally()
// MRR: [[CATCH:%.*]] = alloca ptr, i64 1, align 4
// MRR: [[EXN:%.*]] = alloca { [18 x i32], [4 x ptr] }, i64 1, align 4
// MRR: [[RETHROW:%.*]] = alloca ptr, i64 1, align 4
// MRR: [[BUF:%.*]] = getelementptr inbounds nuw { [18 x i32], [4 x ptr] }, ptr [[EXN]], i32 0, i32 0
// MRR: call void @objc_exception_try_enter(ptr [[EXN]])
// MRR: call i32 @_setjmp(ptr [[BUF]]) #[[SETJMP_CALL]]
// MRR: call void @helper()
// The fallthrough and matched-catch paths run the same @finally body, so
// cir-tail-merge folds the two copies into one block both branch to.
// MRR: br label %[[FINALLY:.*]]
// MRR: [[CAUGHT:%.*]] = call ptr @objc_exception_extract(ptr [[EXN]])
// MRR: store ptr [[CAUGHT]], ptr [[RETHROW]], align 4
// MRR: call void @objc_exception_try_enter(ptr [[EXN]])
// MRR: call i32 @_setjmp(ptr [[BUF]]) #[[SETJMP_CALL]]
// MRR: [[RET:[0-9]+]]:
// MRR-NEXT: ret void
// MRR: call i32 @objc_exception_match(ptr {{.*}}, ptr [[CAUGHT]])
// MRR: br i1 {{.*}}, label %[[MATCH:[^,]+]], label %[[NOMATCH:[^,]+]]
// Re-entering through the setjmp rethrows without a paired try_exit, so this
// copy of the @finally body stays where it is.
// MRR: [[NESTED:%.*]] = load ptr, ptr [[RETHROW]], align 4
// MRR: call void @cleanup()
// MRR: call void @objc_exception_throw(ptr [[NESTED]]) #[[THROW_CALL]]
// MRR: [[MATCH]]:
// MRR: store ptr [[CAUGHT]], ptr [[CATCH]], align 4
// MRR: call void @checkpoint(i32 noundef 1)
// MRR: br label %[[FINALLY]]
// MRR: [[FINALLY]]:
// MRR: call void @objc_exception_try_exit(ptr [[EXN]])
// MRR: call void @cleanup()
// MRR: br label %[[RET]]
// MRR: [[NOMATCH]]:
// MRR: [[UNMATCHED:%.*]] = load ptr, ptr [[RETHROW]], align 4
// MRR: call void @objc_exception_try_exit(ptr [[EXN]])
// MRR: call void @cleanup()
// MRR: call void @objc_exception_throw(ptr [[UNMATCHED]]) #[[THROW_CALL]]

void fragile_arc_catch(void) {
  @try {
    helper();
  } @catch (A *x) {
    checkpoint(1);
  } @catch (id y) {
    checkpoint(2);
  }
}

// ARC-LABEL: define void @fragile_arc_catch()
// ARC: [[X:%.*]] = alloca ptr, i64 1, align 4
// ARC: [[Y:%.*]] = alloca ptr, i64 1, align 4
// ARC: [[EXN:%.*]] = alloca { [18 x i32], [4 x ptr] }, i64 1, align 4
// ARC: [[BUF:%.*]] = getelementptr inbounds nuw { [18 x i32], [4 x ptr] }, ptr [[EXN]], i32 0, i32 0
// ARC: call void @objc_exception_try_enter(ptr [[EXN]])
// ARC: call i32 @_setjmp(ptr [[BUF]]) #[[ARC_SETJMP_CALL:[0-9]+]]
// ARC: call void @helper()
// ARC: call void @objc_exception_try_exit(ptr [[EXN]])
// ARC: [[CAUGHT:%.*]] = call ptr @objc_exception_extract(ptr [[EXN]])
// ARC: call i32 @objc_exception_match(ptr {{.*}}, ptr [[CAUGHT]])
// ARC: [[RETAIN_X:%.*]] = call ptr @llvm.objc.retain(ptr [[CAUGHT]])
// ARC: store ptr [[RETAIN_X]], ptr [[X]], align 4
// ARC: call void @checkpoint(i32 noundef 1)
// ARC: call void @llvm.objc.storeStrong(ptr [[X]], ptr null)
// ARC: [[RETAIN_Y:%.*]] = call ptr @llvm.objc.retain(ptr [[CAUGHT]])
// ARC: store ptr [[RETAIN_Y]], ptr [[Y]], align 4
// ARC: call void @checkpoint(i32 noundef 2)
// ARC: call void @llvm.objc.storeStrong(ptr [[Y]], ptr null)

// MRR: attributes #[[SETJMP_CALL]] = { nounwind returns_twice }
// MRR: attributes #[[THROW_CALL]] = { noreturn nounwind }
// ARC: attributes #[[ARC_SETJMP_CALL]] = { nounwind returns_twice }
