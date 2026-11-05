// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fclangir -o - %s | FileCheck %s

typedef struct {
  int a, b;
  char buf[128];
} Big;

__attribute__((objc_root_class))
@interface Root
- (int)getInt __attribute__((objc_direct));
@property(direct, readonly) int intProperty;
@property(direct, readonly) int intProperty2;
- (void)accessCmd __attribute__((objc_direct));
- (Big)getBig __attribute__((objc_direct));
@end

@interface Root (Cat)
- (int)categoryDirect __attribute__((objc_direct));
@end

@implementation Root
- (int)intProperty2 {
  return 42;
}

- (int)getInt __attribute__((objc_direct)) {
  return 42;
}

+ (int)classGetInt __attribute__((objc_direct)) {
  return 42;
}

- (Big)getBig __attribute__((objc_direct)) {
  Big b = {.a = 42};
  return b;
}

- (void)accessCmd __attribute__((objc_direct)) {
  (void)_cmd;
}
@end

@implementation Root (Cat)
- (int)categoryDirect __attribute__((objc_direct)) {
  return 7;
}
@end

int useRoot(Root *r) {
  return [r getInt] + [r intProperty] + [r intProperty2] +
         [r categoryDirect];
}

// CHECK-LABEL: define hidden i32 @"\01-[Root intProperty2]"(

// CHECK-LABEL: define hidden i32 @"\01-[Root getInt]"(
// CHECK: [[SELF:%.*]] = load ptr, ptr
// CHECK: [[ISNIL:%.*]] = icmp eq ptr [[SELF]], null
// CHECK: br i1 [[ISNIL]]
// CHECK: store i32 0
// CHECK: store i32 42

// CHECK-LABEL: define hidden i32 @"\01+[Root classGetInt]"(
// CHECK: [[CLASSSELF:%.*]] = load ptr, ptr
// CHECK: [[SELFSEL:%.*]] = load ptr, ptr @OBJC_SELECTOR_REFERENCES_
// CHECK: [[REALIZED:%.*]] = call ptr @objc_msgSend(ptr noundef [[CLASSSELF]], ptr noundef [[SELFSEL]])
// CHECK: store ptr [[REALIZED]], ptr
// CHECK: ret i32 42

// CHECK-LABEL: define hidden void @"\01-[Root getBig]"(
// CHECK: [[SELFBIG:%.*]] = load ptr, ptr
// CHECK: [[ISNILBIG:%.*]] = icmp eq ptr [[SELFBIG]], null
// CHECK: br i1 [[ISNILBIG]]
// CHECK: store %struct.Big zeroinitializer

// CHECK-LABEL: define hidden void @"\01-[Root accessCmd]"(
// CHECK: load ptr, ptr @OBJC_SELECTOR_REFERENCES_
// CHECK: store ptr

// CHECK-LABEL: define hidden i32 @"\01-[Root intProperty]"(

// CHECK-LABEL: define hidden i32 @"\01-[Root categoryDirect]"(

// CHECK-LABEL: define i32 @useRoot(
// CHECK-NOT: @OBJC_SELECTOR_REFERENCES_.1
// CHECK: call i32 @"\01-[Root getInt]"(ptr noundef
// CHECK-NOT: @OBJC_SELECTOR_REFERENCES_.2
// CHECK: call i32 @"\01-[Root intProperty]"(ptr noundef
// CHECK-NOT: @OBJC_SELECTOR_REFERENCES_.3
// CHECK: call i32 @"\01-[Root intProperty2]"(ptr noundef
// CHECK-NOT: @"\01-[Root(Cat) categoryDirect]"
// CHECK: call i32 @"\01-[Root categoryDirect]"(ptr noundef
