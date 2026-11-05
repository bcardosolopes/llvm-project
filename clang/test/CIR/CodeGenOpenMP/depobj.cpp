// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -std=c++11 -fopenmp -fclangir -emit-cir %s -o /dev/null
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -std=c++11 -fopenmp -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -std=c++11 -fopenmp-simd -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=SIMD
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

typedef void *omp_depend_t;

void depobj_all_memory() {
  omp_depend_t obj;
#pragma omp depobj(obj) depend(inout: omp_all_memory)
  (void)obj;
}

template <class T> T depobj_template(T argc) {
  static T a;
  int *argv;
#pragma omp depobj(a) depend(in: argv, ([3][*(int *)argv][4])argv)
#pragma omp depobj(argc) destroy
#pragma omp depobj(argc) update(inout)
  return argc;
}

int main(int argc, char **argv) {
  static omp_depend_t a;
  omp_depend_t b;
#pragma omp depobj(a) depend(out: argc, argv)
#pragma omp depobj(b) destroy
#pragma omp depobj(b) update(mutexinoutset)
#pragma omp depobj(a) depend(iterator(char *p = argv[argc] : argv[0] : -1), out: p[0])
  (void)depobj_template(a), depobj_template(b);
  depobj_all_memory();
  return 0;
}

// LLVM-LABEL: define void @_Z17depobj_all_memoryv()
// LLVM: call i32 @__kmpc_global_thread_num(
// LLVM: %[[ALLOC_ALL:.+]] = call noalias ptr @__kmpc_alloc(i32 %{{.+}}, i64 48, ptr null)
// LLVM: %[[ALL_COUNT:.+]] = getelementptr inbounds nuw { i64, i64, i8 }, ptr %[[ALLOC_ALL]], i32 0, i32 0
// LLVM: store i64 1, ptr %[[ALL_COUNT]], align 8
// LLVM: %[[ALL_REC:.+]] = getelementptr inbounds { i64, i64, i8 }, ptr %[[ALLOC_ALL]], i64 %{{.+}}
// LLVM: %[[ALL_BASE:.+]] = getelementptr inbounds nuw { i64, i64, i8 }, ptr %[[ALL_REC]], i32 0, i32 0
// LLVM: store i64 0, ptr %[[ALL_BASE]], align 8
// LLVM: store i64 0, ptr %{{.+}}, align 8
// LLVM: store i8 -128, ptr %{{.+}}, align 8
// LLVM: %[[ALL_FIRST:.+]] = getelementptr inbounds { i64, i64, i8 }, ptr %[[ALLOC_ALL]], i64 1
// LLVM: store ptr %[[ALL_FIRST]], ptr %{{.+}}, align 8

// LLVM-LABEL: define linkonce_odr noundef ptr @_Z15depobj_templateIPvET_S1_(
// LLVM: %[[T_ALLOC:.+]] = call noalias ptr @__kmpc_alloc(i32 %{{.+}}, i64 72, ptr null)
// LLVM: %[[T_COUNT:.+]] = getelementptr inbounds nuw { i64, i64, i8 }, ptr %[[T_ALLOC]], i32 0, i32 0
// LLVM: store i64 2, ptr %[[T_COUNT]], align 8
// LLVM: store i64 8, ptr %{{.+}}, align 8
// LLVM: %[[SHAPE_MUL0:.+]] = mul i64 12, %{{.+}}
// LLVM: %[[SHAPE_SIZE:.+]] = mul i64 %[[SHAPE_MUL0]], 4
// LLVM: store i64 %[[SHAPE_SIZE]], ptr %{{.+}}, align 8
// LLVM: call void @__kmpc_free(i32 %{{.+}}, ptr %{{.+}}, ptr null)
// LLVM: store i8 3, ptr %{{.+}}, align 8

// LLVM-LABEL: define noundef i32 @main(
// LLVM: %[[M_ALLOC:.+]] = call noalias ptr @__kmpc_alloc(i32 %{{.+}}, i64 72, ptr null)
// LLVM: %[[M_COUNT:.+]] = getelementptr inbounds nuw { i64, i64, i8 }, ptr %[[M_ALLOC]], i32 0, i32 0
// LLVM: store i64 2, ptr %[[M_COUNT]], align 8
// LLVM: store i64 4, ptr %{{.+}}, align 8
// LLVM: store i8 3, ptr %{{.+}}, align 8
// LLVM: store i64 8, ptr %{{.+}}, align 8
// LLVM: store i8 3, ptr %{{.+}}, align 8
// LLVM: store ptr %{{.+}}, ptr @_ZZ4mainE1a, align 8
// LLVM: call void @__kmpc_free(i32 %{{.+}}, ptr %{{.+}}, ptr null)
// LLVM: store i8 4, ptr %{{.+}}, align 8
// LLVM: %[[ITER_ELEMS:.+]] = mul i64 1, %{{.+}}
// LLVM: %[[ITER_RECS:.+]] = add i64 %[[ITER_ELEMS]], 1
// LLVM: %[[ITER_BYTES:.+]] = mul i64 %[[ITER_RECS]], 24
// LLVM: %[[ITER_ALLOC:.+]] = call noalias ptr @__kmpc_alloc(i32 %{{.+}}, i64 %[[ITER_BYTES]], ptr null)
// LLVM: store i64 %[[ITER_ELEMS]], ptr %{{.+}}, align 8
// LLVM: store i64 1, ptr %{{.+}}, align 8
// LLVM: store i64 0, ptr %{{.+}}, align 8
// LLVM: store i64 %{{.+}}, ptr %{{.+}}, align 8
// LLVM: store i64 1, ptr %{{.+}}, align 8
// LLVM: store i8 3, ptr %{{.+}}, align 8

// SIMD-NOT: {{__kmpc|__tgt}}

// OGCG-LABEL: define {{.*}} @_Z17depobj_all_memoryv()
// OGCG:   call i32 @__kmpc_global_thread_num(
// OGCG:   call ptr @__kmpc_alloc(
// OGCG: define {{.*}} @main(
// OGCG:   call i32 @__kmpc_global_thread_num(
// OGCG:   call ptr @__kmpc_alloc(
// OGCG:   call void @__kmpc_free(
// OGCG:   call ptr @__kmpc_alloc(
// OGCG: define linkonce_odr {{.*}} @_Z15depobj_templateIPvET_S1_(
// OGCG:   call i32 @__kmpc_global_thread_num(
// OGCG:   call ptr @__kmpc_alloc(
// OGCG:   call void @__kmpc_free(
