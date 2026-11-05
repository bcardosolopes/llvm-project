// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// C99 static array parameters should get align + dereferenceable attributes
// on the decayed pointer, matching OGCG.

// LLVM: define dso_local void @f_int_10(ptr noundef align 4 dereferenceable(40) %{{.*}})
// OGCG: define dso_local void @f_int_10(ptr noundef align 4 dereferenceable(40) %{{.*}})
void f_int_10(int a[static 10]) { a[0] = 1; }

// LLVM: define dso_local void @f_int_1(ptr noundef align 4 dereferenceable(4) %{{.*}})
// OGCG: define dso_local void @f_int_1(ptr noundef align 4 dereferenceable(4) %{{.*}})
void f_int_1(int a[static 1]) { a[0] = 1; }

// LLVM: define dso_local void @f_float_5(ptr noundef align 4 dereferenceable(20) %{{.*}})
// OGCG: define dso_local void @f_float_5(ptr noundef align 4 dereferenceable(20) %{{.*}})
void f_float_5(float a[static 5]) { a[0] = 1.0f; }

// LLVM: define dso_local void @f_double_3(ptr noundef align 8 dereferenceable(24) %{{.*}})
// OGCG: define dso_local void @f_double_3(ptr noundef align 8 dereferenceable(24) %{{.*}})
void f_double_3(double a[static 3]) { a[0] = 1.0; }

// Non-static array: no extra attributes.
// LLVM: define dso_local void @f_no_static(ptr noundef %{{.*}})
// OGCG: define dso_local void @f_no_static(ptr noundef %{{.*}})
void f_no_static(int a[10]) { a[0] = 1; }

// VLA with static: nonnull + align but no dereferenceable (size unknown).
// LLVM: define dso_local void @f_vla_static(i32 noundef %{{.*}}, ptr noundef nonnull align 4 %{{.*}})
// OGCG: define dso_local void @f_vla_static(i32 noundef %{{.*}}, ptr noundef nonnull align 4 %{{.*}})
void f_vla_static(int n, int a[static n]) { a[0] = 1; }
