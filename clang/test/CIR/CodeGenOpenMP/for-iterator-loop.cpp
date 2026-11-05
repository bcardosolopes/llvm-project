// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -fclangir -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fopenmp -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

struct Bool {
  Bool(bool b) : b(b) {}
  operator bool() const { return b; }
  const bool b;
};

template <typename T>
struct It {
  It() : p(0) {}
  It(const It &, int = 0);
  template <typename U>
  It(U &, int = 0);
  It &operator=(const It &);
  It &operator=(It &);
  ~It() {}

  It(T *p) : p(p) {}

  operator T *&() { return p; }
  operator T *() const { return p; }
  T *operator->() const { return p; }

  It &operator++() {
    ++p;
    return *this;
  }

  It &operator+=(unsigned n) {
    p += n;
    return *this;
  }

  T *p;
};

typedef Bool BoolType;

template <typename T>
BoolType operator<(It<T> a, It<T> b) {
  return a.p < b.p;
}

// LLVM-LABEL: define {{.*}} @_Z12loop_with_It2ItIcES0_(
// LLVM: call void @__kmpc_for_static_init_8(
// LLVM: omp.loop_nest.region:
// LLVM-NEXT: store i64 %{{.*}}, ptr %{{[0-9]+}}, align 8
// LLVM-NEXT: %[[BEGIN_IT:.*]] = load ptr, ptr %{{[0-9]+}}, align 8{{.*}}
// LLVM-NEXT: %[[BEGIN_PTR_REF:.*]] = call {{.*}} @_ZN2ItIcEcvRPcEv(ptr {{.*}} %[[BEGIN_IT]])
// LLVM-NEXT: %[[BEGIN_PTR:.*]] = load ptr, ptr %[[BEGIN_PTR_REF]], align 8
// LLVM-NEXT: %[[LOGICAL:.*]] = load i64, ptr %{{[0-9]+}}, align 8
// LLVM-NEXT: %[[OFFSET:.*]] = mul nsw i64 %[[LOGICAL]], 1
// LLVM-NEXT: %[[ADDR:.*]] = getelementptr inbounds i8, ptr %[[BEGIN_PTR]], i64 %[[OFFSET]]
// LLVM-NEXT: call void @_ZN2ItIcEC1EPc(ptr {{.*}}, ptr noundef %[[ADDR]])
// LLVM: call void @__kmpc_for_static_fini(
void loop_with_It(It<char> begin, It<char> end) {
#pragma omp for
  for (It<char> it = begin; it < end; ++it)
    *it = 0;
}

// LLVM-LABEL: define {{.*}} @_Z17loop_with_It_plus2ItIcES0_(
// LLVM: call void @__kmpc_for_static_init_8(
// LLVM: call void @__kmpc_for_static_fini(
void loop_with_It_plus(It<char> begin, It<char> end) {
#pragma omp for
  for (It<char> it = begin; it < end; it += 1u)
    *it = 0;
}

// OGCG-LABEL: define {{.*}} @_Z12loop_with_It2ItIcES0_(
// OGCG: call void @__kmpc_for_static_init_8(
// OGCG: call void @__kmpc_for_static_fini(

// OGCG-LABEL: define {{.*}} @_Z17loop_with_It_plus2ItIcES0_(
// OGCG: call void @__kmpc_for_static_init_8(
// OGCG: call void @__kmpc_for_static_fini(
