// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=macosx-fragile-10.5 -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=macosx-fragile-10.5 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-apple-darwin -fobjc-runtime=macosx-fragile-10.5 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

id g;

int test_constant_p_objc_ptr(void) {
  return __builtin_constant_p(g);
}

// LLVM: define{{.*}} i32 @test_constant_p_objc_ptr()
// LLVM:   %[[G:.*]] = load ptr, ptr @g
// LLVM:   %[[ISCONST:.*]] = call i1 @llvm.is.constant.p0(ptr %[[G]])
// LLVM:   %[[ZEXT:.*]] = zext i1 %[[ISCONST]] to i32
// LLVM:   ret i32 %[[ZEXT]]
// LLVM: declare i1 @llvm.is.constant.p0(ptr)

// OGCG: define{{.*}} i32 @test_constant_p_objc_ptr()
// OGCG:   %[[G:.*]] = load ptr, ptr @g
// OGCG:   %[[ISCONST:.*]] = call i1 @llvm.is.constant.p0(ptr %[[G]])
// OGCG:   %[[ZEXT:.*]] = zext i1 %[[ISCONST]] to i32
// OGCG:   ret i32 %[[ZEXT]]
// OGCG: declare i1 @llvm.is.constant.p0(ptr)
