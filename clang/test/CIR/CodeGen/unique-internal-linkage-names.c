// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -funique-internal-linkage-names %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -funique-internal-linkage-names %s -o - | FileCheck %s

// Verify that -funique-internal-linkage-names emits
// "sample-profile-suffix-elision-policy"="selected" on both function
// definitions and call sites for internal-linkage functions.

inline void overloaded_external() {}
extern void overloaded_external();

static void overloaded_internal() {}
extern void overloaded_internal();

void markUsed() {
  overloaded_external();
  overloaded_internal();
}

// CHECK-DAG: define internal void @overloaded_internal() [[FNATTR:#[0-9]+]]
// CHECK-DAG: call void @overloaded_internal() [[CALLATTR:#[0-9]+]]
// CHECK-DAG: attributes [[FNATTR]] = {{.*}}"sample-profile-suffix-elision-policy"="selected"
// CHECK-DAG: attributes [[CALLATTR]] = {{.*}}"sample-profile-suffix-elision-policy"="selected"
