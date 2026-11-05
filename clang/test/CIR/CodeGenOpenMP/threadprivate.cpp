// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fnoopenmp-use-tls -fclangir -emit-llvm %s -o - | FileCheck %s

struct S {
  int x;
  S(int v = 0) : x(v) {}
  S(const S &) = default;
  S &operator=(const S &) = default;
  ~S() {}
};

S gs(1);
#pragma omp threadprivate(gs)

int use_global() { return gs.x; }

int use_static() {
  static S ls(2);
#pragma omp threadprivate(ls)
  return ls.x;
}

void use_copyin() {
  static S tp(3);
#pragma omp threadprivate(tp)
#pragma omp parallel copyin(tp)
  ++tp.x;
}

// CHECK: define internal noundef ptr @__kmpc_threadprivate_ctor.gs(ptr
// CHECK: define internal void @__kmpc_threadprivate_dtor.gs(ptr
// CHECK: call void @__kmpc_threadprivate_register(ptr @{{.*}}, ptr @gs, ptr @__kmpc_threadprivate_ctor.gs, ptr null, ptr @__kmpc_threadprivate_dtor.gs)
//
// CHECK: define internal noundef ptr @__kmpc_threadprivate_ctor._ZZ10use_staticvE2ls(ptr
// CHECK: define internal void @__kmpc_threadprivate_dtor._ZZ10use_staticvE2ls(ptr
// CHECK-LABEL: define dso_local noundef i32 @_Z10use_staticv()
// CHECK: call void @__kmpc_threadprivate_register(ptr @{{.*}}, ptr @_ZZ10use_staticvE2ls, ptr @__kmpc_threadprivate_ctor._ZZ10use_staticvE2ls, ptr null, ptr @__kmpc_threadprivate_dtor._ZZ10use_staticvE2ls)
//
// CHECK: define internal noundef ptr @__kmpc_threadprivate_ctor._ZZ10use_copyinvE2tp(ptr
// CHECK: define internal void @__kmpc_threadprivate_dtor._ZZ10use_copyinvE2tp(ptr
// CHECK-LABEL: define dso_local void @_Z10use_copyinv()
// CHECK: call void @__kmpc_threadprivate_register(ptr @{{.*}}, ptr @_ZZ10use_copyinvE2tp, ptr @__kmpc_threadprivate_ctor._ZZ10use_copyinvE2tp, ptr null, ptr @__kmpc_threadprivate_dtor._ZZ10use_copyinvE2tp)
// CHECK-LABEL: define internal void @_Z10use_copyinv..omp_par
// CHECK: call ptr @__kmpc_threadprivate_cached(ptr @{{.*}}, i32 %{{.*}}, ptr @_ZZ10use_copyinvE2tp, i64 4, ptr @_ZZ10use_copyinvE2tp.cache)
