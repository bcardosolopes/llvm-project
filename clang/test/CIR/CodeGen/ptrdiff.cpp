// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

typedef unsigned long size_type;

size_type size(unsigned long *_start, unsigned long *_finish) {
  // CIR-LABEL: cir.func {{.*}} @_Z4sizePmS_
  // CIR: %[[D:.*]] = cir.ptr_diff {{.*}} : !cir.ptr<!u64i> -> !s64i
  // CIR: %[[U:.*]] = cir.cast integral %[[D]] : !s64i -> !u64i
  // CIR: cir.return {{.*}} : !u64i

  // LLVM-LABEL: define dso_local {{.*}}i64 @_Z4sizePmS_(
  // LLVM: %[[IA:.*]] = ptrtoint ptr %{{.*}} to i64
  // LLVM: %[[IB:.*]] = ptrtoint ptr %{{.*}} to i64
  // LLVM: %[[SUB:.*]] = sub i64 %[[IA]], %[[IB]]
  // LLVM: %[[Q:.*]] = sdiv exact i64 %[[SUB]], 8
  // LLVM: ret i64 %[[Q]]

  // OGCG-LABEL: define dso_local {{.*}}i64 @_Z4sizePmS_(
  // OGCG: %[[IA:.*]] = ptrtoint ptr %{{.*}} to i64
  // OGCG: %[[IB:.*]] = ptrtoint ptr %{{.*}} to i64
  // OGCG: %[[SUB:.*]] = sub i64 %[[IA]], %[[IB]]
  // OGCG: %[[Q:.*]] = sdiv exact i64 %[[SUB]], 8
  // OGCG: ret i64 %[[Q]]

  return static_cast<size_type>(_finish - _start);
}

// Regression test: long double (x86_fp80) pointer difference must use the
// alloc size (16 bytes on x86_64) rather than the store size (10 bytes).
long long ptrdiff_long_double(long double *a, long double *b) {
  // CIR-LABEL: cir.func {{.*}} @_Z19ptrdiff_long_double
  // CIR: cir.ptr_diff {{.*}} : !cir.ptr<!cir.long_double<!cir.f80>> -> !s64i

  // LLVM-LABEL: define dso_local {{.*}}i64 @_Z19ptrdiff_long_double
  // LLVM: %[[IA:.*]] = ptrtoint ptr %{{.*}} to i64
  // LLVM: %[[IB:.*]] = ptrtoint ptr %{{.*}} to i64
  // LLVM: %[[SUB:.*]] = sub i64 %[[IA]], %[[IB]]
  // LLVM: %[[Q:.*]] = sdiv exact i64 %[[SUB]], 16
  // LLVM: ret i64 %[[Q]]

  // OGCG-LABEL: define dso_local {{.*}}i64 @_Z19ptrdiff_long_double
  // OGCG: %[[IA:.*]] = ptrtoint ptr %{{.*}} to i64
  // OGCG: %[[IB:.*]] = ptrtoint ptr %{{.*}} to i64
  // OGCG: %[[SUB:.*]] = sub i64 %[[IA]], %[[IB]]
  // OGCG: %[[Q:.*]] = sdiv exact i64 %[[SUB]], 16
  // OGCG: ret i64 %[[Q]]

  return a - b;
}
