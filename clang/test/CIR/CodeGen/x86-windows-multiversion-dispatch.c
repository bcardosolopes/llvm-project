// RUN: %clang_cc1 -triple x86_64-windows-pc -fclangir -emit-llvm -O0 %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-windows-pc -emit-llvm -O0 %s -o - | FileCheck %s
//
// Verify direct-CIR matches OGCG's Win64 x86 multiversion dispatch semantics:
// - target(...) uses a .resolver dispatcher with the original signature.
// - target_clones(...) uses the public base symbol as the dispatcher.

int __attribute__((target("sse4.2"))) mv(int x) { return x + 1; }
int __attribute__((target("arch=ivybridge"))) mv(int x) { return x + 2; }
int __attribute__((target("default"))) mv(int x) { return x + 3; }

typedef int (*mv_ptr_t)(int);
void consume(mv_ptr_t);

int use_mv(int x) {
  consume(mv);
  mv_ptr_t p = mv;
  mv_ptr_t q = &mv;
  return p(x) + q(x);
}

__attribute__((target_clones("default", "sse4.2")))
int clones(int x) {
  return x + 4;
}

int use_clones(int x) { return clones(x); }

int use_clones_ptr(int x) {
  consume(clones);
  mv_ptr_t p = clones;
  return p(x);
}

// CHECK-DAG: @__cpu_model = external dso_local global { i32, i32, i32, [1 x i32] }
// CHECK-DAG: define dso_local i32 @mv.sse4.2(i32
// CHECK-DAG: define dso_local i32 @mv.arch_ivybridge(i32
// CHECK-DAG: define dso_local i32 @mv(i32
// CHECK-DAG: define dso_local i32 @use_mv(i32
// CHECK-DAG: call void @consume(ptr noundef @mv.resolver)
// CHECK-DAG: store ptr @mv.resolver
// CHECK-DAG: define weak_odr dso_local i32 @mv.resolver(i32{{( noundef)?}} %0){{.*}}comdat
// CHECK-DAG: call void @__cpu_indicator_init()
// CHECK-DAG: musttail call i32 @mv.arch_ivybridge(i32{{( noundef)?}} %0)
// CHECK-DAG: musttail call i32 @mv.sse4.2(i32{{( noundef)?}} %0)
// CHECK-DAG: musttail call i32 @mv(i32{{( noundef)?}} %0)
// CHECK-DAG: define dso_local i32 @clones.default.1(i32
// CHECK-DAG: define dso_local i32 @clones.sse4.2.0(i32
// CHECK-DAG: define weak_odr dso_local i32 @clones(i32{{( noundef)?}} %0){{.*}}comdat
// CHECK-DAG: musttail call i32 @clones.sse4.2.0(i32{{( noundef)?}} %0)
// CHECK-DAG: musttail call i32 @clones.default.1(i32{{( noundef)?}} %0)
// CHECK-DAG: define dso_local i32 @use_clones(i32
// CHECK-DAG: call i32 @clones(i32
// CHECK-DAG: define dso_local i32 @use_clones_ptr(i32
// CHECK-DAG: call void @consume(ptr noundef @clones)
// CHECK-DAG: store ptr @clones
