// RUN: %clang_cc1 -std=c++11 -triple x86_64-apple-darwin10 -fclangir -emit-llvm %s -o - | FileCheck %s

struct Struct {
  constexpr static const char *name = "foo";
};

void use(int, const char *);
Struct *getPtr();

void scalarStaticVariableInMemberExpr(Struct *ptr, Struct &ref) {
  use(1, Struct::name);
  Struct s;
  use(2, s.name);
  use(3, ptr->name);
  use(4, ref.name);
  use(5, Struct().name);
  use(6, getPtr()->name);
}

// CHECK: @[[STR:.*]] = private unnamed_addr constant [4 x i8] c"foo\00", align 1
// CHECK-NOT: @_ZN6Struct4nameE.const
// CHECK-LABEL: define void @_Z32scalarStaticVariableInMemberExprP6StructRS_
// CHECK: call void @_Z3useiPKc(i32 noundef 1, ptr noundef @[[STR]])
// CHECK: call void @_Z3useiPKc(i32 noundef 2, ptr noundef @[[STR]])
// CHECK: call void @_Z3useiPKc(i32 noundef 3, ptr noundef @[[STR]])
// CHECK: call void @_Z3useiPKc(i32 noundef 4, ptr noundef @[[STR]])
// CHECK: call void @_Z3useiPKc(i32 noundef 5, ptr noundef @[[STR]])
// CHECK: call void @_Z3useiPKc(i32 noundef 6, ptr noundef @[[STR]])
