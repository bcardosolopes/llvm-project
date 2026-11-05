// RUN: %clang_cc1 -triple x86_64-apple-macosx10.15.0 -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-apple-macosx10.15.0 -fclangir -emit-llvm %s -o /dev/null

// Verify that forward ObjC interface and category declarations
// do not crash CIR codegen (they require no code generation).

@interface NSObject
@end

@interface MyClass : NSObject
@end

@interface MyClass (MyCategory)
@end
