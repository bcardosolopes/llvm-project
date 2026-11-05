// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Inline/template variable initializers need individual comdat-keyed
// entries in @llvm.global_ctors, not just a single _GLOBAL__sub_I_ entry.

int f();

// Non-inline variable — folded into _GLOBAL__sub_I_.
int a = f();

// Inline variable — needs its own comdat-keyed entry.
inline int b = f();

// Another non-inline variable — also folded into _GLOBAL__sub_I_.
int c = f();

// Template variable — needs its own comdat-keyed entry.
template<typename T> inline int d = f();
int e = d<int>;

// Both CIR and OGCG should produce 3 entries in @llvm.global_ctors:
//   1. __cxx_global_var_init.1 keyed to @b
//   2. __cxx_global_var_init.4 keyed to @_Z1dIiE
//   3. _GLOBAL__sub_I_ with null (for non-comdat inits a, c, e)

// CHECK: @llvm.global_ctors = appending global [3 x { i32, ptr, ptr }]
// CHECK-SAME: ptr @__cxx_global_var_init.1, ptr @b
// CHECK-SAME: ptr @__cxx_global_var_init.4, ptr @_Z1dIiE
// CHECK-SAME: ptr @_GLOBAL__sub_I_{{.*}}, ptr null

// OGCG: @llvm.global_ctors = appending global [3 x { i32, ptr, ptr }]
// OGCG-SAME: ptr @__cxx_global_var_init.1, ptr @b
// OGCG-SAME: ptr @__cxx_global_var_init.4, ptr @_Z1dIiE
// OGCG-SAME: ptr @_GLOBAL__sub_I_{{.*}}, ptr null

// Comdat init functions should have comdat groups.
// CHECK: define internal void @__cxx_global_var_init.1(){{.*}} comdat($b)
// CHECK: define internal void @__cxx_global_var_init.4(){{.*}} comdat($_Z1dIiE)

// OGCG: define internal void @__cxx_global_var_init.1(){{.*}} comdat($b)
// OGCG: define internal void @__cxx_global_var_init.4(){{.*}} comdat($_Z1dIiE)

// _GLOBAL__sub_I_ should only call non-comdat inits (a, c, e), not b or d.
// CHECK-LABEL: define internal void @_GLOBAL__sub_I_
// CHECK: call void @__cxx_global_var_init()
// CHECK: call void @__cxx_global_var_init.2()
// CHECK: call void @__cxx_global_var_init.3()
// CHECK-NOT: call void @__cxx_global_var_init.1()
// CHECK-NOT: call void @__cxx_global_var_init.4()

// OGCG-LABEL: define internal void @_GLOBAL__sub_I_
// OGCG: call void @__cxx_global_var_init()
// OGCG: call void @__cxx_global_var_init.2()
// OGCG: call void @__cxx_global_var_init.3()
// OGCG-NOT: call void @__cxx_global_var_init.1()
// OGCG-NOT: call void @__cxx_global_var_init.4()
