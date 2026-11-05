// RUN: %clang_cc1 -triple=x86_64-apple-darwin10 -emit-llvm -std=c++14 -fblocks -fobjc-arc -fclangir -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple=x86_64-apple-darwin10 -emit-llvm -std=c++14 -fblocks -fobjc-arc -o - %s | FileCheck %s --check-prefix=OGCG

namespace CaptureByReference {

struct S {
  int val() const;
  int a;
  S();
  S(const S &);
  S &operator=(const S &);
  S(S &&);
  S &operator=(S &&);
};

S getS();

// LLVM-LABEL: define internal noundef i32 @"_ZZN18CaptureByReference5test2EvENK3$_0clIiEEDaT_"(
// LLVM: %[[BLOCK_CAPTURED:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %{{.*}}, i32 0, i32 5
// LLVM: %[[V0:.*]] = getelementptr inbounds nuw %[[LAMBDA_REF:.*]], ptr %{{.*}}, i32 0, i32 0
// LLVM: %[[V1:.*]] = load ptr, ptr %[[V0]], align 8
// LLVM: store ptr %[[V1]], ptr %[[BLOCK_CAPTURED]], align 8
// OGCG-LABEL: define internal noundef i32 @"_ZZN18CaptureByReference5test2EvENK3$_0clIiEEDaT_"(
// OGCG: %[[BLOCK_CAPTURED:.*]] = getelementptr inbounds nuw <{ ptr, i32, i32, ptr, {{.*}}, ptr }>, ptr %{{.*}}, i32 0, i32 5
// OGCG: %[[V0:.*]] = getelementptr inbounds nuw %class.anon{{(\.0)?}}, ptr %{{.*}}, i32 0, i32 0
// OGCG: %[[V1:.*]] = load ptr, ptr %[[V0]], align 8
// OGCG: store ptr %[[V1]], ptr %[[BLOCK_CAPTURED]], align 8
int test2() {
  S s;
  auto fn = [&](const auto a) {
    return ^{
      return s.val();
    }();
  };
  return fn(123);
}

// LLVM-LABEL: define internal noundef i32 @"_ZZN18CaptureByReference5test3EvENK3$_0clIiEEDaT_"(
// LLVM: %[[BLOCK_CAPTURED:.*]] = getelementptr inbounds nuw <{ ptr, i32, i32, ptr, ptr, %"struct.CaptureByReference::S" }>, ptr %{{.*}}, i32 0, i32 5
// LLVM: %[[V0:.*]] = getelementptr inbounds nuw %[[LAMBDA:.*]], ptr %{{.*}}, i32 0, i32 0
// LLVM: call void @_ZN18CaptureByReference1SC1ERKS0_(ptr {{[^,]*}} %[[BLOCK_CAPTURED]], ptr {{.*}} %[[V0]])
// OGCG-LABEL: define internal noundef i32 @"_ZZN18CaptureByReference5test3EvENK3$_0clIiEEDaT_"(
// OGCG: %[[BLOCK:.*]] = alloca <{ ptr, i32, i32, ptr, ptr, %"struct.CaptureByReference::S" }>, align 8
// OGCG: %[[BLOCK_CAPTURED:.*]] = getelementptr inbounds nuw <{ ptr, i32, i32, ptr, ptr, %"struct.CaptureByReference::S" }>, ptr %[[BLOCK]], i32 0, i32 5
// OGCG: %[[V0:.*]] = getelementptr inbounds nuw %[[LAMBDA:.*]], ptr %{{.*}}, i32 0, i32 0
// OGCG: call void @_ZN18CaptureByReference1SC1ERKS0_(ptr {{[^,]*}} %[[BLOCK_CAPTURED]], ptr {{.*}} %[[V0]])
int test3() {
  const S &s = getS();
  auto fn = [=](const auto a) {
    return ^{
      return s.val();
    }();
  };
  return fn(123);
}

} // namespace CaptureByReference
