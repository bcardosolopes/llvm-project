// RUN: %clang_cc1 -std=c++17 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.x86.ll
// RUN: FileCheck --input-file=%t.x86.ll %s

// Test that getNewInitValue handles ConstArrayAttr with non-ArrayAttr
// elements (e.g. StringAttr for string literals) during global type
// replacement. Previously, this crashed with an assertion in
// ArrayAttr::getValue() because the code assumed getElts() always
// returns an ArrayAttr.

// Forward-declare with incomplete type, then define — causes type
// replacement when the global's type changes from incomplete to complete.
extern const char kName[];
struct Entry {
  const char* name;
  int value;
};

// Reference kName in a global initializer that nests ConstArrayAttr.
const Entry entries[] = {
  {kName, 1},
  {kName, 2},
};

// Keep a function-body GetGlobal user before completing kName so the
// replacement path must reconcile both function and global users.
const char *predef_ptr() { return kName; }

// Complete the definition — triggers global replacement.
const char kName[] = "example_name";

// CHECK: @kName = {{.*}}constant [13 x i8] c"example_name\00"
// CHECK: @_ZL7entries = {{.*}}constant [2 x
// CHECK: define{{.*}} ptr @_Z10predef_ptrv
// CHECK: define{{.*}} ptr @_Z11postdef_ptrv
int get_value() { return entries[0].value; }
const char *postdef_ptr() { return kName; }
