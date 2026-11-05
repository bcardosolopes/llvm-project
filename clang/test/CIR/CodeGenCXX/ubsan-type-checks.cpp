// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=null -o - %s | FileCheck %s --check-prefix=CHECK-NULL
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=null,vptr -o - %s | FileCheck %s --check-prefix=CHECK-VPTR
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=vptr -fsanitize-trap=vptr -o - %s | FileCheck %s --check-prefix=CHECK-TRAP
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=null,vptr -o - %s | grep -Ec '^@\.src(\.| =)' \
// RUN:   | FileCheck %s --check-prefix=SRC-COUNT
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -emit-llvm \
// RUN:   -fsanitize=null,vptr -o - %s | grep -Ec '^@\.src(\.| =)' \
// RUN:   | FileCheck %s --check-prefix=SRC-COUNT
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -fclangir -emit-llvm \
// RUN:   -fsanitize=vptr -fsanitize-trap=vptr -o - %s \
// RUN:   | sed -n '/private unnamed_addr global { { ptr, i32, i32 }, ptr, ptr, i8 }/p' \
// RUN:   | count 0
// RUN: %clang_cc1 -std=c++11 -triple x86_64-unknown-linux -emit-llvm \
// RUN:   -fsanitize=vptr -fsanitize-trap=vptr -o - %s \
// RUN:   | sed -n '/private unnamed_addr global { { ptr, i32, i32 }, ptr, ptr, i8 }/p' \
// RUN:   | count 0

struct T {
  virtual ~T() {}
  virtual int v() { return 1; }
};

struct U : T {
  ~U();
  virtual int v() { return 2; }
};

U::~U() {}

// CHECK-NULL-LABEL: define {{.*}} @_Z5get_vP1T
int get_v(T* t) {
  // Null check before vtable load.
  // CHECK-NULL: [[NONNULL:%.*]] = icmp ne ptr
  // CHECK-NULL: br i1 [[NONNULL]], label %{{.*}}, label %{{.*}}, !prof ![[PROF:[0-9]+]], !nosanitize ![[NOSAN:[0-9]+]]
  // CHECK-NULL: call void @__ubsan_handle_type_mismatch_v1_abort(ptr {{.*}}) #[[CALLATTR:[0-9]+]], !nosanitize ![[NOSAN]]
  return t->v();
}

// CHECK-VPTR-LABEL: define {{.*}} @_Z9delete_itP1T
void delete_it(T *t) {
  // Null + vptr check before delete.
  // CHECK-VPTR: br i1 {{.*}}, label %{{.*}}, label %{{.*}}, !prof ![[PROF:[0-9]+]], !nosanitize ![[NOSAN:[0-9]+]]
  // CHECK-VPTR: call void @__ubsan_handle_type_mismatch_v1_abort(ptr {{.*}}) #[[CALLATTR:[0-9]+]], !nosanitize ![[NOSAN]]
  // CHECK-VPTR: %[[HASH:.*]] = xor i64 7267112328930554169, %{{.*}}, !nosanitize ![[NOSAN]]
  // CHECK-VPTR: %[[SLOT:.*]] = and i64 %[[HASH]], 127, !nosanitize ![[NOSAN]]
  // CHECK-VPTR: getelementptr inbounds [128 x i64], ptr @__ubsan_vptr_type_cache, i32 0, i64 %[[SLOT]]
  // CHECK-VPTR: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
  // CHECK-VPTR: call void @__ubsan_handle_dynamic_type_cache_miss_abort(ptr {{.*}}) #[[VPTRCALL:[0-9]+]], !nosanitize ![[NOSAN]]
  delete t;
}

// CHECK-VPTR-LABEL: define {{.*}} @_Z7dyncastP1T
U* dyncast(T *t) {
  // Vptr check before dynamic_cast.
  // CHECK-VPTR: [[VPTR_NONNULL:%.*]] = icmp ne ptr {{.*}}, null, !nosanitize ![[DYNC_NOSAN:[0-9]+]]
  // CHECK-VPTR: br i1 [[VPTR_NONNULL]], label %{{.*}}, label %{{.*}}, !nosanitize ![[DYNC_NOSAN]]
  // CHECK-VPTR: getelementptr inbounds [128 x i64], ptr @__ubsan_vptr_type_cache
  // CHECK-VPTR: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !prof ![[DYNC_PROF:[0-9]+]], !nosanitize ![[DYNC_NOSAN]]
  // CHECK-VPTR: call void @__ubsan_handle_dynamic_type_cache_miss_abort
  // CHECK-VPTR: [[DYNCAST_NONNULL:%.*]] = icmp ne ptr {{.*}}, null
  // CHECK-VPTR: br i1 [[DYNCAST_NONNULL]], label %{{.*}}, label %{{.*}}
  return dynamic_cast<U*>(t);
}

// CHECK-TRAP-LABEL: define {{.*}} @_Z9delete_itP1T
// CHECK-TRAP: %[[HASH:.*]] = xor i64 7267112328930554169, %{{.*}}, !nosanitize ![[NOSAN:[0-9]+]]
// CHECK-TRAP: %[[SLOT:.*]] = and i64 %[[HASH]], 127, !nosanitize ![[NOSAN]]
// CHECK-TRAP: getelementptr inbounds [128 x i64], ptr @__ubsan_vptr_type_cache, i32 0, i64 %[[SLOT]]
// CHECK-TRAP: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !prof ![[PROF:[0-9]+]], !nosanitize ![[NOSAN]]
// CHECK-TRAP: call void @llvm.ubsantrap(i8 4) #[[NOMERGE:[0-9]+]], !nosanitize ![[NOSAN]]
// CHECK-TRAP-NEXT: unreachable, !nosanitize ![[NOSAN]]
// CHECK-TRAP: attributes #[[NOMERGE]] = {{{.*}}nomerge{{.*}}noreturn{{.*}}nounwind{{.*}}}

// SRC-COUNT: 1

// Method prologue 'this' null check.
// CHECK-NULL-LABEL: define {{.*}} @_ZN1U1vEv
// CHECK-NULL: icmp ne ptr
// CHECK-NULL: br i1 {{.*}}, label %{{.*}}, label %{{.*}}, !prof ![[PROF]], !nosanitize ![[NOSAN]]
// CHECK-NULL: call void @__ubsan_handle_type_mismatch_v1_abort(ptr {{.*}}) #[[CALLATTR]]
