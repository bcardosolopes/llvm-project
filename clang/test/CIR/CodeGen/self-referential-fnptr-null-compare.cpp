// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-mlir=cir-abi \
// RUN:   -std=gnu++20 -w %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Regression test: a self-referential function pointer field can be
// refreshed from a placeholder !cir.func<()> member type to the final
// signature after constructor IR was already emitted. Existing field-address
// users must be repaired with pointer bitcasts so the placeholder-typed
// loads/stores remain valid until the field is rewritten with the final type.

template <typename Sig>
struct function;

template <typename R, typename... Args>
struct function<R(Args...)> {
  void *ptr;
  function() : ptr(nullptr) {}
};

template <typename T, typename D>
struct unique_ptr {
  T *ptr;
  D del;
  unique_ptr() : ptr(nullptr), del() {}
};

template <typename T, unsigned long N = static_cast<unsigned long>(-1)>
struct span {
  T *data_;
  unsigned long size_;

  constexpr span(T *d, unsigned long s) : data_(d), size_(s) {}
};

using Completion = void (*)(bool, void *);

template <typename T>
struct S {
  using F = void (*)(span<S<T> *const>, int, Completion, void *);

  int state_ = 0;
  bool flag_ = false;
  F f_ = nullptr;
  unique_ptr<int, function<void(int *)>> extra_;
  int method_;

  S(F g = nullptr) : f_(g) {
    if (f_ == nullptr)
      f_ = defaultInit;
  }

  static void defaultInit(span<S<T> *const>, int, Completion, void *) {}
};

template class S<unsigned char>;

// CHECK-LABEL: cir.func {{.*}} @_ZN1SIhEC2EPFv4spanIKPS0_Lm18446744073709551615EEiPFvbPvES5_
// CHECK: %[[THIS_SLOT:.*]] = cir.alloca !cir.ptr<!rec_S3Cunsigned_char3E>, !cir.ptr<!cir.ptr<!rec_S3Cunsigned_char3E>>, ["this", init]
// CHECK: %[[ARG_SLOT:.*]] = cir.alloca !cir.ptr<!cir.func<()>>, !cir.ptr<!cir.ptr<!cir.func<()>>>, ["g", init]
// CHECK: cir.store %arg1, %[[ARG_SLOT]] : !cir.ptr<!cir.func<()>>, !cir.ptr<!cir.ptr<!cir.func<()>>>
// CHECK: %[[THIS:.*]] = cir.load %[[THIS_SLOT]] : !cir.ptr<!cir.ptr<!rec_S3Cunsigned_char3E>>, !cir.ptr<!rec_S3Cunsigned_char3E>
// CHECK: %[[INIT_FIELD:.*]] = cir.get_member %[[THIS]][3] {name = "f_"} : !cir.ptr<!rec_S3Cunsigned_char3E> -> !cir.ptr<!cir.ptr<!cir.func<{{.*}}>>>
// CHECK: %[[INIT:.*]] = cir.load align(8) %[[ARG_SLOT]] : !cir.ptr<!cir.ptr<!cir.func<()>>>, !cir.ptr<!cir.func<()>>
// CHECK: %[[INIT_FIELD_CAST:.*]] = cir.cast bitcast %[[INIT_FIELD]] : !cir.ptr<!cir.ptr<!cir.func<{{.*}}>>> -> !cir.ptr<!cir.ptr<!cir.func<()>>>
// CHECK: cir.store align(8) %[[INIT]], %[[INIT_FIELD_CAST]] : !cir.ptr<!cir.func<()>>, !cir.ptr<!cir.ptr<!cir.func<()>>>
// CHECK: cir.call @_ZN10unique_ptrIi8functionIFvPiEEEC1Ev
// CHECK: %[[LOAD_FIELD:.*]] = cir.get_member %[[THIS]][3] {name = "f_"} : !cir.ptr<!rec_S3Cunsigned_char3E> -> !cir.ptr<!cir.ptr<!cir.func<{{.*}}>>>
// CHECK: %[[LOAD_FIELD_CAST:.*]] = cir.cast bitcast %[[LOAD_FIELD]] : !cir.ptr<!cir.ptr<!cir.func<{{.*}}>>> -> !cir.ptr<!cir.ptr<!cir.func<()>>>
// CHECK: %[[LOAD:.*]] = cir.load align(8) %[[LOAD_FIELD_CAST]] : !cir.ptr<!cir.ptr<!cir.func<()>>>, !cir.ptr<!cir.func<()>>
// CHECK: %[[NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!cir.func<()>>
// CHECK: %[[CMP:.*]] = cir.cmp(eq, %[[LOAD]], %[[NULL]]) : !cir.ptr<!cir.func<()>>, !cir.bool
// CHECK: cir.if %[[CMP]] {
// CHECK: %[[DEFAULT:.*]] = cir.get_global @_ZN1SIhE11defaultInitE4spanIKPS0_Lm18446744073709551615EEiPFvbPvES5_ : !cir.ptr<!cir.func<{{.*}}>>
// CHECK: %[[CAST:.*]] = cir.cast bitcast %[[DEFAULT]]
// CHECK: %[[STORE_FIELD:.*]] = cir.get_member %[[THIS]][3] {name = "f_"} : !cir.ptr<!rec_S3Cunsigned_char3E> -> !cir.ptr<!cir.ptr<!cir.func<{{.*}}>>>
// CHECK: %[[STORE_FIELD_CAST:.*]] = cir.cast bitcast %[[STORE_FIELD]] : !cir.ptr<!cir.ptr<!cir.func<{{.*}}>>> -> !cir.ptr<!cir.ptr<!cir.func<()>>>
// CHECK: cir.store align(8) %[[CAST]], %[[STORE_FIELD_CAST]]
