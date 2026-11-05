// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -fobjc-runtime=ios-16.0.0 -fobjc-arc -fblocks -x objective-c++ -std=gnu++20 -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple arm64-apple-ios16.0.0 -fobjc-runtime=ios-16.0.0 -fobjc-arc -fblocks -x objective-c++ -std=gnu++20 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// Assigning to a __weak-qualified Objective-C lvalue must emit objc_storeWeak.
// The scalar-assignment lvalue path previously handled only __strong and
// __autoreleasing and fell through to errorNYI for __weak.

__attribute__((objc_root_class)) @interface O @end
@implementation O @end

// LLVM-LABEL: define{{.*}}weak_assign
// LLVM: call {{.*}}@llvm.objc.storeWeak
// OGCG-LABEL: define{{.*}}weak_assign
// OGCG: call {{.*}}@llvm.objc.storeWeak
void weak_assign(__weak O **slot, O *x) { *slot = x; }

// __unsafe_unretained is an ordinary store (no ARC bookkeeping) and must also
// compile (OCL_ExplicitNone falls through to the plain store).
// LLVM-LABEL: define{{.*}}unsafe_assign
// LLVM-NOT: call {{.*}}storeWeak
void unsafe_assign(__unsafe_unretained O **slot, O *x) { *slot = x; }
