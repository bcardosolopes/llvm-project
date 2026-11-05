// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test that new-expression with noexcept operator new (which produces a null
// check conditional) combined with aggregate initialization of a struct with
// non-trivially destructible members doesn't produce SSA dominance violations.
//
// The per-member destruction cleanups pushed during aggregate initialization
// inside the conditional true branch must be deactivated before it exits,
// otherwise later throwing calls' cleanup regions would reference
// cir.get_member SSA values defined inside that conditional region.

typedef unsigned long size_t;
void *malloc(size_t);
void free(void *);

struct Ref {
  void *ptr_;
  Ref(void *p);  // may throw
  ~Ref();
};

struct Context {
  // noexcept operator new triggers a null-check conditional
  static void *operator new(size_t n) noexcept { return malloc(n); }
  static void operator delete(void *p) { free(p); }
  Ref a;
  Ref b;
  Ref c;
};

void may_throw(Context *);

// CHECK-LABEL: cir.func{{.*}} @_Z4testPvS_S_
// CHECK:   %[[RESULT:.*]] = cir.alloca !cir.ptr<!rec_Context>, !cir.ptr<!cir.ptr<!rec_Context>>, ["new.result"]
// CHECK:   cir.if %{{.*}} {
// CHECK:     cir.cast bitcast
// CHECK:     cir.get_member
// CHECK:     cir.store align(8) %{{.*}}, %[[RESULT]]
// CHECK:   } else {
// CHECK:     cir.store align(8) %{{.*}}, %[[RESULT]]
// CHECK:   }

// LLVM-LABEL: define{{.*}} void @_Z4testPvS_S_
// LLVM:   call {{.*}}@_ZN7ContextnwEm(
// LLVM:   icmp ne ptr
void test(void *x, void *y, void *z) {
  Context *ctx = new Context{Ref(x), Ref(y), Ref(z)};
  may_throw(ctx);
}
