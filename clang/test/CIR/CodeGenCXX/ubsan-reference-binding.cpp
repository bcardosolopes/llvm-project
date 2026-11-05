// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=alignment,null,object-size -o - %s | FileCheck %s

struct S {
  double d;
  int a, b;
};

// CHECK-LABEL: define {{.*}} @_Z17reference_bindingPiP1S
void reference_binding(int *p, S *q) {
  // CHECK: %[[P:.*]] = load ptr, ptr %{{.*}}, align 8
  // CHECK: %[[PNONNULL:.*]] = icmp ne ptr %[[P]], null, !nosanitize ![[NOSAN:[0-9]+]]
  // CHECK: %[[PSIZE:.*]] = call i64 @llvm.objectsize.i64.p0(ptr %[[P]], i1 false, i1 false, i1 false)
  // CHECK: %[[POK:.*]] = icmp uge i64 %[[PSIZE]], 4, !nosanitize ![[NOSAN]]
  // CHECK: %[[PINT1:.*]] = ptrtoint ptr %[[P]] to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[PMASK:.*]] = and i64 %[[PINT1]], 3, !nosanitize ![[NOSAN]]
  // CHECK: %[[PALIGN:.*]] = icmp eq i64 %[[PMASK]], 0, !nosanitize ![[NOSAN]]
  // CHECK: %[[PINT2:.*]] = ptrtoint ptr %[[P]] to i64, !nosanitize ![[NOSAN]]
  // CHECK: br i1 {{.*}}, label %{{.*}}, label %{{.*}}, !prof ![[PROF:[0-9]+]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_type_mismatch_v1_abort(ptr {{.*}}, i64 %[[PINT2]]) #[[CALLATTR:[0-9]+]], !nosanitize ![[NOSAN]]
  int &r = *p;

  // CHECK: %[[Q:.*]] = load ptr, ptr %{{.*}}, align 8
  // CHECK: %[[QNONNULL:.*]] = icmp ne ptr %[[Q]], null, !nosanitize ![[NOSAN]]
  // CHECK: %[[QSIZE:.*]] = call i64 @llvm.objectsize.i64.p0(ptr %[[Q]], i1 false, i1 false, i1 false)
  // CHECK: %[[QOK:.*]] = icmp uge i64 %[[QSIZE]], 16, !nosanitize ![[NOSAN]]
  // CHECK: %[[QINT1:.*]] = ptrtoint ptr %[[Q]] to i64, !nosanitize ![[NOSAN]]
  // CHECK: %[[QMASK:.*]] = and i64 %[[QINT1]], 7, !nosanitize ![[NOSAN]]
  // CHECK: %[[QALIGN:.*]] = icmp eq i64 %[[QMASK]], 0, !nosanitize ![[NOSAN]]
  // CHECK: %[[QINT2:.*]] = ptrtoint ptr %[[Q]] to i64, !nosanitize ![[NOSAN]]
  // CHECK: br i1 {{.*}}, label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK: call void @__ubsan_handle_type_mismatch_v1_abort(ptr {{.*}}, i64 %[[QINT2]]) #[[CALLATTR]], !nosanitize ![[NOSAN]]
  // CHECK-NOT: @__ubsan_handle_dynamic_type_cache_miss
  S &r2 = *q;

  (void)r;
  (void)r2;
}

// CHECK: declare void @__ubsan_handle_type_mismatch_v1_abort(ptr, i64) #[[DECLATTR:[0-9]+]]
// CHECK: attributes #[[DECLATTR]] = { noreturn nounwind uwtable }
// CHECK: attributes #[[CALLATTR]] = { nomerge noreturn nounwind }
