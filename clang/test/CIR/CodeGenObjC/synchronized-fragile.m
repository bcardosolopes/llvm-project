// RUN: %clang_cc1 -triple i686-apple-darwin9 -fobjc-runtime=macosx-fragile-10.5 -fobjc-exceptions -emit-llvm -fclangir -o - %s | FileCheck %s

extern void opaque(void);

// CHECK: ; Function Attrs: nonlazybind returns_twice
// CHECK-NEXT: declare i32 @_setjmp(ptr) #[[SETJMP_DECL:[0-9]+]]

void synchronized_return(id a) {
  @synchronized(a) {
    opaque();
    return;
  }
}

// CHECK-LABEL: define{{.*}} void @synchronized_return(
// CHECK: [[ARG_SLOT:%.*]] = alloca ptr, i64 1, align 4
// CHECK: [[SYNC_ARG:%.*]] = alloca ptr, i64 1, align 4
// CHECK: [[EXN:%.*]] = alloca { [18 x i32], [4 x ptr] }, i64 1, align 4
// CHECK: call i32 @objc_sync_enter(ptr %{{.*}})
// CHECK: call void @objc_exception_try_enter(ptr [[EXN]])
// CHECK: [[SETJMP_BUF:%.*]] = getelementptr inbounds nuw { [18 x i32], [4 x ptr] }, ptr [[EXN]], i32 0, i32 0
// CHECK: call i32 @_setjmp(ptr [[SETJMP_BUF]]) #[[SETJMP_CALL:[0-9]+]]
// CHECK: call void asm sideeffect "", "*m,*m,*m{{.*}}"(ptr elementtype(ptr) [[ARG_SLOT]], ptr elementtype(ptr) [[SYNC_ARG]], ptr elementtype({ [18 x i32], [4 x ptr] }) [[EXN]])
// CHECK-NEXT: call void @opaque()
// CHECK: call void @objc_exception_try_exit(ptr [[EXN]])
// CHECK: call i32 @objc_sync_exit(ptr %{{.*}})
// CHECK: call void asm sideeffect "", "=*m,=*m,=*m{{.*}}"(ptr elementtype(ptr) [[ARG_SLOT]], ptr elementtype(ptr) [[SYNC_ARG]], ptr elementtype({ [18 x i32], [4 x ptr] }) [[EXN]])
// CHECK: call i32 @objc_sync_exit(ptr %{{.*}})
// CHECK: [[CAUGHT:%.*]] = call ptr @objc_exception_extract(ptr [[EXN]])
// CHECK: call void @objc_exception_throw(ptr [[CAUGHT]]) #[[THROW_CALL:[0-9]+]]

void early_return(id a) {
  @synchronized(({ return; }), a) {
    opaque();
  }
}

// CHECK-LABEL: define{{.*}} void @early_return(
// CHECK: ret void
// CHECK-NOT: objc_sync_enter
// CHECK: }
// CHECK: attributes #[[SETJMP_CALL]] = { nounwind returns_twice }
// CHECK: attributes #[[THROW_CALL]] = { noreturn nounwind }
