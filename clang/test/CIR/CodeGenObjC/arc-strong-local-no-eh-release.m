// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -fobjc-arc -fexceptions \
// RUN:   -fobjc-runtime=ios -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=NOEXC
// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -fobjc-arc -fexceptions \
// RUN:   -fobjc-arc-exceptions -fobjc-runtime=ios -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=EXC

// Regression test: a __strong local's objc_release/objc_storeStrong cleanup
// must use getARCCleanupKind() semantics. By default (-fno-objc-arc-exceptions)
// ARC deliberately leaks on unwind, so the release must run on the normal-exit
// path only -- NOT on the exception landing pad (which would over-release/UAF
// if the exception is caught upstack). WITH -fobjc-arc-exceptions the release
// runs on both paths.

__attribute__((objc_root_class)) @interface O @end
@implementation O @end

id make_obj(void);
void may_throw(void);

void f(void) {
  __strong id x = make_obj();
  may_throw();
}

// Without ARC exceptions: no landing pad, plain call, release on normal exit.
// NOEXC-LABEL: define void @f()
// NOEXC-NOT:     invoke void @may_throw()
// NOEXC:         call void @may_throw()
// NOEXC:         call void @llvm.objc.storeStrong(ptr {{.*}}, ptr null)
// NOEXC-NOT:     landingpad
// NOEXC:       }

// With ARC exceptions: may_throw becomes an invoke and the landing pad also
// releases x.
// EXC-LABEL: define void @f()
// EXC:         invoke void @may_throw()
// EXC:         landingpad
// EXC:         call void @llvm.objc.storeStrong(ptr {{.*}}, ptr null)
// EXC:       }
