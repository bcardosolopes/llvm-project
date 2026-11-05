// RUN: %clang_cc1 -x objective-c++ -fblocks -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-fragile-10.5 -std=c++1z -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -x objective-c++ -fblocks -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-fragile-10.5 -std=c++1z -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

typedef void (^BlockTy)(void);
void takesBlock(BlockTy);

struct Copyable {
  Copyable(const Copyable &);
};

void test(Copyable x) {
  takesBlock([x] {});
}

void testLambdaVar(Copyable x) {
  auto lambda = [x] {};
  takesBlock(lambda);
}

// LLVM-LABEL: define internal void @{{.*}}__copy_helper_block_8_32c{{.*}}(ptr noundef
// LLVM: call void @{{.*}}C1ERKS0_
// LLVM-LABEL: define internal void @__destroy_helper_block_8_32(ptr noundef
// LLVM: ret void
// LLVM-LABEL: define{{.*}} void @_Z4test8Copyable(
// LLVM: call void @{{.*}}C1ERKS0_
// LLVM: call void @{{.*}}takesBlock{{.*}}(ptr noundef
// LLVM-LABEL: define internal noundef ptr @"_ZZ13testLambdaVar8CopyableENK3$_0cvU13block_pointerFvvEEv"(
// LLVM: %[[COPY_SEL:.*]] = load ptr, ptr @OBJC_SELECTOR_REFERENCES_
// LLVM: %[[COPY:.*]] = call noundef ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %[[COPY_SEL]])
// LLVM: %[[AUTORELEASE_SEL:.*]] = load ptr, ptr @OBJC_SELECTOR_REFERENCES_.1
// LLVM: %[[AUTORELEASE:.*]] = call noundef ptr @objc_msgSend(ptr noundef %[[COPY]], ptr noundef %[[AUTORELEASE_SEL]])
// LLVM: store ptr %[[AUTORELEASE]], ptr %{{.*}}, align 8
// LLVM: ret ptr %{{.*}}
// LLVM-LABEL: define{{.*}} void @_Z13testLambdaVar8Copyable(
// LLVM: %[[BLOCKPTR:.*]] = call noundef ptr @"_ZZ13testLambdaVar8CopyableENK3$_0cvU13block_pointerFvvEEv"(ptr noundef nonnull align 1 dereferenceable(1) %[[LAMBDA:.*]])
// LLVM: call void @{{.*}}takesBlock{{.*}}(ptr noundef %[[BLOCKPTR]])

// OGCG-LABEL: define{{.*}} void @_Z4test8Copyable(
// OGCG: call void @{{.*}}C1ERKS0_
// OGCG: call void @{{.*}}takesBlock{{.*}}(ptr noundef
// OGCG-LABEL: define{{.*}} void @_Z13testLambdaVar8Copyable(
// OGCG: %[[BLOCKPTR:.*]] = call noundef ptr @"_ZZ13testLambdaVar8CopyableENK3$_0cvU13block_pointerFvvEEv"(ptr noundef nonnull align 1 dereferenceable(1) %[[LAMBDA:.*]])
// OGCG: call void @{{.*}}takesBlock{{.*}}(ptr noundef %[[BLOCKPTR]])
