// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fexceptions -fobjc-exceptions -fobjc-runtime-has-weak -fclangir -o - %s | FileCheck %s --check-prefix=CHECK-MRR
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fexceptions -fobjc-exceptions -fobjc-runtime-has-weak -fclangir -o - %s | FileCheck %s --check-prefix=CHECK-ARC

@class Ety;

void helper(void);
void helperNoThrow(void) __attribute__((nothrow));
void cleanup(void) __attribute__((nothrow));
void die(void) __attribute__((nothrow, noreturn));

void rethrow(void) {
  @try {
    helper();
  } @catch (id e) {
    @throw;
  }
}

// CHECK-MRR-LABEL: define{{.*}} void @rethrow()
// CHECK-MRR-SAME: personality ptr @__objc_personality_v0
// CHECK-MRR: invoke void @helper()
// CHECK-MRR: landingpad { ptr, i32 }
// CHECK-MRR: catch ptr @OBJC_EHTYPE_id
// CHECK-MRR: call ptr @objc_begin_catch(ptr {{.*}})
// CHECK-MRR: invoke void @objc_exception_rethrow()

void catchAll(void) {
  @try {
    helper();
  } @catch (...) {
  }
}

// CHECK-MRR-LABEL: define{{.*}} void @catchAll()
// CHECK-MRR-SAME: personality ptr @__objc_personality_v0
// CHECK-MRR: invoke void @helper()
// CHECK-MRR: landingpad { ptr, i32 }
// CHECK-MRR: catch ptr null
// CHECK-MRR: call ptr @objc_begin_catch(ptr {{.*}})
// CHECK-MRR: call void @objc_end_catch()

void rethrowCatchAll(void) {
  @try {
    helper();
  } @catch (...) {
    @throw;
  }
}

// CHECK-MRR-LABEL: define{{.*}} void @rethrowCatchAll()
// CHECK-MRR-SAME: personality ptr @__objc_personality_v0
// CHECK-MRR: invoke void @helper()
// CHECK-MRR: landingpad { ptr, i32 }
// CHECK-MRR: catch ptr null
// CHECK-MRR: call ptr @objc_begin_catch(ptr {{.*}})
// CHECK-MRR: invoke void @objc_exception_rethrow()
// CHECK-MRR: landingpad { ptr, i32 }
// CHECK-MRR: cleanup
// CHECK-MRR: invoke void @objc_end_catch()

void finallyNormal(void) {
  @try {
    helper();
  } @finally {
    cleanup();
  }
}

// CHECK-MRR-LABEL: define{{.*}} void @finallyNormal()
// CHECK-MRR-SAME: personality ptr @__objc_personality_v0
// CHECK-MRR: invoke void @helper()
// CHECK-MRR: call void @cleanup()
// CHECK-MRR: landingpad { ptr, i32 }
// CHECK-MRR: catch ptr null
// CHECK-MRR: call ptr @objc_begin_catch(ptr {{.*}})
// CHECK-MRR: call void @cleanup()
// CHECK-MRR: invoke void @objc_exception_rethrow()
// CHECK-MRR: landingpad { ptr, i32 }
// CHECK-MRR: cleanup
// CHECK-MRR: invoke void @objc_end_catch()

void finallyNoReturn(void) {
  @try {
    die();
  } @finally {
    cleanup();
  }
}

// CHECK-MRR-LABEL: define{{.*}} void @finallyNoReturn()
// CHECK-MRR-NOT: call void @cleanup()
// CHECK-MRR: call void @die()
// CHECK-MRR-NEXT: unreachable

int finallyReturn(void) {
  @try {
    return 1;
  } @finally {
    cleanup();
  }
}

// CHECK-MRR-LABEL: define{{.*}} i32 @finallyReturn()
// CHECK-MRR: call void @cleanup()
// CHECK-MRR: ret i32 1

void finallyGoto(void) {
  @try {
    goto done;
  } @finally {
    cleanup();
  }
done:
  helper();
}

// CHECK-MRR-LABEL: define{{.*}} void @finallyGoto()
// CHECK-MRR: call void @cleanup()
// CHECK-MRR: call void @helper()

void finallyBreak(void) {
  while (1) {
    @try {
      break;
    } @finally {
      cleanup();
    }
  }
  helper();
}

// CHECK-MRR-LABEL: define{{.*}} void @finallyBreak()
// CHECK-MRR: call void @cleanup()
// CHECK-MRR: call void @helper()

void catchFinally(void) {
  @try {
    helper();
  } @catch (...) {
    helperNoThrow();
  } @finally {
    cleanup();
  }
}

// CHECK-MRR-LABEL: define{{.*}} void @catchFinally()
// CHECK-MRR-SAME: personality ptr @__objc_personality_v0
// CHECK-MRR: invoke void @helper()
// CHECK-MRR: landingpad { ptr, i32 }
// CHECK-MRR: catch ptr null
// CHECK-MRR: call ptr @objc_begin_catch(ptr {{.*}})
// CHECK-MRR: call void @helperNoThrow()
// CHECK-MRR: invoke void @objc_end_catch()
// CHECK-MRR: call void @cleanup()

void rethrowCatchFinally(void) {
  @try {
    helper();
  } @catch (...) {
    @throw;
  } @finally {
    cleanup();
  }
}

// CHECK-MRR-LABEL: define{{.*}} void @rethrowCatchFinally()
// CHECK-MRR-SAME: personality ptr @__objc_personality_v0
// CHECK-MRR: invoke void @helper()
// CHECK-MRR: landingpad { ptr, i32 }
// CHECK-MRR: catch ptr null
// CHECK-MRR: call ptr @objc_begin_catch(ptr {{.*}})
// CHECK-MRR: invoke void @objc_exception_rethrow()
// CHECK-MRR: landingpad { ptr, i32 }
// CHECK-MRR: invoke void @objc_end_catch()
// CHECK-MRR: call ptr @objc_begin_catch(ptr {{.*}})
// CHECK-MRR: call void @cleanup()
// CHECK-MRR: invoke void @objc_exception_rethrow()

void testStrong(void) {
  @try {
    helper();
  } @catch (Ety *e) {
  }
}

// CHECK-ARC-LABEL: define{{.*}} void @testStrong()
// CHECK-ARC-SAME: personality ptr @__objc_personality_v0
// CHECK-ARC: [[E:%.*]] = alloca ptr, i64 1, align 8
// CHECK-ARC: invoke void @helper()
// CHECK-ARC: [[T0:%.*]] = call ptr @objc_begin_catch(ptr {{.*}})
// CHECK-ARC-NEXT: [[T1:%.*]] = call ptr @llvm.objc.retain(ptr [[T0]])
// CHECK-ARC-NEXT: store ptr [[T1]], ptr [[E]]
// CHECK-ARC-NEXT: call void @llvm.objc.storeStrong(ptr [[E]], ptr null)
// CHECK-ARC-NEXT: call void @objc_end_catch()

#if __has_feature(objc_arc)
void testWeak(void) {
  @try {
    helper();
  } @catch (__weak Ety *e) {
  }
}

// CHECK-ARC-LABEL: define{{.*}} void @testWeak()
// CHECK-ARC-SAME: personality ptr @__objc_personality_v0
// CHECK-ARC: [[WEAK:%.*]] = alloca ptr, i64 1, align 8
// CHECK-ARC: invoke void @helper()
// CHECK-ARC: [[T2:%.*]] = call ptr @objc_begin_catch(ptr {{.*}})
// CHECK-ARC-NEXT: call ptr @llvm.objc.initWeak(ptr [[WEAK]], ptr [[T2]])
// CHECK-ARC-NEXT: call void @llvm.objc.destroyWeak(ptr [[WEAK]])
// CHECK-ARC-NEXT: call void @objc_end_catch()
#endif
