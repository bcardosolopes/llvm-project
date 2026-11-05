// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=array-bounds -o - %s | FileCheck %s

struct ArrayMembers {
  int a1[5];
  int a2[1];
};

// CHECK-LABEL: define {{.*}} @_Z11array_indexRA4_Kii
int array_index(const int (&a)[4], int n) {
  // CHECK: %[[IDX1:.*]] = sext i32 %{{.*}} to i64, !nosanitize ![[NOSAN:[0-9]+]]
  // CHECK: %[[OK1:.*]] = icmp ult i64 %[[IDX1]], 4, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[OK1]], label %{{.*}}, label %{{.*}}, !prof ![[PROF:[0-9]+]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_out_of_bounds_abort(ptr {{.*}}, i64 %[[IDX1]]) #[[CALLATTR:[0-9]+]], !nosanitize ![[NOSAN]]
  int k1 = a[n];

  // CHECK: %[[IDX2:.*]] = sext i32 %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[OK2:.*]] = icmp ule i64 %[[IDX2]], 4, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[OK2]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_out_of_bounds_abort(ptr {{.*}}, i64 %[[IDX2]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
  const int *r1 = &a[n];

  // CHECK: %[[IDX3:.*]] = sext i32 %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[OK3:.*]] = icmp ult i64 %[[IDX3]], 8, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[OK3]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_out_of_bounds_abort(ptr {{.*}}, i64 %[[IDX3]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
  int k2 = ((const int(&)[8])a)[n];

  // CHECK: %[[IDX4:.*]] = sext i32 %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[OK4:.*]] = icmp ult i64 %[[IDX4]], 4, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[OK4]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_out_of_bounds_abort(ptr {{.*}}, i64 %[[IDX4]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
  int k3 = n[a];

  return k1 + *r1 + k2 + k3;
}

// CHECK-LABEL: define {{.*}} @_Z17multi_array_indexii
int multi_array_index(int n, int m) {
  int arr[4][6];

  // CHECK: %[[OUTER:.*]] = sext i32 %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[OUTER_OK:.*]] = icmp ult i64 %[[OUTER]], 4, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[OUTER_OK]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_out_of_bounds_abort(ptr {{.*}}, i64 %[[OUTER]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]

  // CHECK: %[[INNER:.*]] = sext i32 %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[INNER_OK:.*]] = icmp ult i64 %[[INNER]], 6, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[INNER_OK]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_out_of_bounds_abort(ptr {{.*}}, i64 %[[INNER]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
  return arr[n][m];
}

// CHECK-LABEL: define {{.*}} @_Z11array_arithRA4_Kii
int array_arith(const int (&a)[4], int n) {
  // CHECK: %[[ARITH1:.*]] = sext i32 %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[ARITH1_OK:.*]] = icmp ule i64 %[[ARITH1]], 4, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[ARITH1_OK]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_out_of_bounds_abort(ptr {{.*}}, i64 %[[ARITH1]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
  const int *k1 = a + n;

  // CHECK: %[[ARITH2:.*]] = sext i32 %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[ARITH2_OK:.*]] = icmp ule i64 %[[ARITH2]], 8, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[ARITH2_OK]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_out_of_bounds_abort(ptr {{.*}}, i64 %[[ARITH2]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
  const int *k2 = (const int(&)[8])a + n;

  return *k1 + *k2;
}

// CHECK-LABEL: define {{.*}} @_Z18struct_array_indexP12ArrayMembersi
int struct_array_index(ArrayMembers *p, int n) {
  // CHECK: %[[STRUCTIDX:.*]] = sext i32 %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[STRUCTOK:.*]] = icmp ult i64 %[[STRUCTIDX]], 5, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[STRUCTOK]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_out_of_bounds_abort(ptr {{.*}}, i64 %[[STRUCTIDX]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
  return p->a1[n];
}

// CHECK-LABEL: define {{.*}} @_Z16flex_array_indexP12ArrayMembersi
int flex_array_index(ArrayMembers *p, int n) {
  // CHECK-NOT: @__ubsan_handle_out_of_bounds
  // CHECK: ret i32
  return p->a2[n];
}

extern int incomplete[];
// CHECK-LABEL: define {{.*}} @_Z22incomplete_array_indexi
int incomplete_array_index(int n) {
  // CHECK-NOT: @__ubsan_handle_out_of_bounds
  // CHECK: ret i32
  return incomplete[n];
}

typedef __attribute__((ext_vector_type(4))) int V4I;
// CHECK-LABEL: define {{.*}} @_Z12vector_indexDv4_ii
int vector_index(V4I v, int n) {
  // CHECK: %[[VECIDX:.*]] = sext i32 %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[VECOK:.*]] = icmp ult i64 %[[VECIDX]], 4, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[VECOK]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_out_of_bounds_abort(ptr {{.*}}, i64 %[[VECIDX]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
  return v[n];
}

// CHECK-LABEL: define {{.*}} @_Z12string_indexi
char string_index(int n) {
  // CHECK: %[[STRIDX:.*]] = sext i32 %{{.*}} to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[STROK:.*]] = icmp ult i64 %[[STRIDX]], 6, !nosanitize ![[NOSAN]]
  // CHECK: br i1 %[[STROK]], label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_out_of_bounds_abort(ptr {{.*}}, i64 %[[STRIDX]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
  return "Hello"[n];
}

// CHECK: declare void @__ubsan_handle_out_of_bounds_abort(ptr, i64) #[[DECLATTR:[0-9]+]]
// CHECK: attributes #[[DECLATTR]] = { noreturn nounwind uwtable }
// CHECK: attributes #[[CALLATTR]] = { nomerge noreturn nounwind }
