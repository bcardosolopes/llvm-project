// RUN: %clang_cc1 -x objective-c -triple x86_64-apple-macosx11.0.0 -fobjc-arc -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -x objective-c -triple x86_64-apple-macosx11.0.0 -fobjc-arc -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// The per-ivar type string in the instance-variables list must be built with
// the ivar FieldDecl so record fields are expanded with their member names and
// bitfields are encoded with their width (the "b<width>" form), matching
// classic CodeGen.

@interface Root {
  Class isa;
}
@end
@implementation Root
@end

struct Mixed {
  int i;
  double d;
  char c;
};

@interface C : Root {
  struct Mixed _mv;
  int _bf : 3;
}
@end
@implementation C
@end

// LLVM: @[[MV:OBJC_METH_VAR_TYPE_.*]] = private unnamed_addr constant {{.*}} c"{Mixed=\22i\22i\22d\22d\22c\22c}\00"
// LLVM: @[[BF:OBJC_METH_VAR_TYPE_.*]] = private unnamed_addr constant [3 x i8] c"b3\00"

// OGCG: @[[MV:OBJC_METH_VAR_TYPE_.*]] = private unnamed_addr constant {{.*}} c"{Mixed=\22i\22i\22d\22d\22c\22c}\00"
// OGCG: @[[BF:OBJC_METH_VAR_TYPE_.*]] = private unnamed_addr constant [3 x i8] c"b3\00"
