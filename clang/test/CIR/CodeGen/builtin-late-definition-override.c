// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// Prototype-only at call site, then locally defined later in the TU.
// Lowering must consult the final callee symbol definition, not just spelling.
static double __builtin_modf(double, double *);
static double __builtin_frexp(double, int *);
static void __builtin_coro_resume(void *);
static double modf(double, double *);
static void sincos(double, double *, double *);
static void __builtin_sincos(double, double *, double *);
static void __builtin_sincospi(double, double *, double *);
static _Bool __builtin_coro_done(void *);
static _Bool __builtin_coro_alloc(void);
static long long __builtin_coro_size(void);
static long long __builtin_coro_align(void);
static void *__builtin_coro_noop(void);
static void *__builtin_coro_frame(void);
static void *__builtin_coro_begin(void *);
static void *__builtin_coro_free(void *);
static void __builtin_coro_end(void *, _Bool);
static void *__builtin_coro_promise(void *, int, _Bool);
static int __builtin_coro_suspend(void *, int);
static int __builtin_coro_id(int, void *, void *, void *);
static int __builtin_coro_save(void *);
static void __builtin_coro_destroy(void *);
static _Bool __builtin_coro_await_suspend_bool(void *, void *, void *);
static void *__builtin_coro_await_suspend_handle(void *, void *, void *);
static void __builtin_coro_await_suspend_void(void *, void *, void *);

double test_late_defined_builtin_modf(double x, double *iptr) {
  return __builtin_modf(x, iptr);
}

// CIR-LABEL: @test_late_defined_builtin_modf
// CIR: call double @__builtin_modf(double noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.modf.

// OGCG-LABEL: @test_late_defined_builtin_modf
// OGCG: call double @__builtin_modf(double noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.modf.

double test_late_defined_builtin_frexp(double x, int *eptr) {
  return __builtin_frexp(x, eptr);
}

// CIR-LABEL: @test_late_defined_builtin_frexp
// CIR: call double @__builtin_frexp(double noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.frexp.

// OGCG-LABEL: @test_late_defined_builtin_frexp
// OGCG: call double @__builtin_frexp(double noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.frexp.

double test_late_defined_modf_alias(double x, double *iptr) {
  return modf(x, iptr);
}

// CIR-LABEL: @test_late_defined_modf_alias
// CIR: call double @modf(double noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.modf.

// OGCG-LABEL: @test_late_defined_modf_alias
// OGCG: call double @modf(double noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.modf.

void test_late_defined_builtin_coro_resume(void *h) {
  __builtin_coro_resume(h);
}

// CIR-LABEL: @test_late_defined_builtin_coro_resume
// CIR: call void @__builtin_coro_resume(ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_resume
// OGCG: call void @__builtin_coro_resume(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

void test_late_defined_sincos_alias(double x, double *sinp, double *cosp) {
  sincos(x, sinp, cosp);
}

// CIR-LABEL: @test_late_defined_sincos_alias
// CIR: call void @sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.sincos.

// OGCG-LABEL: @test_late_defined_sincos_alias
// OGCG: call void @sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.sincos.

void test_late_defined_builtin_sincospi(double x, double *sinp, double *cosp) {
  __builtin_sincospi(x, sinp, cosp);
}

// CIR-LABEL: @test_late_defined_builtin_sincospi
// CIR: call void @__builtin_sincospi(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.sincospi.

// OGCG-LABEL: @test_late_defined_builtin_sincospi
// OGCG: call void @__builtin_sincospi(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.sincospi.

void test_late_defined_builtin_sincos(double x, double *sinp, double *cosp) {
  __builtin_sincos(x, sinp, cosp);
}

// CIR-LABEL: @test_late_defined_builtin_sincos
// CIR: call void @__builtin_sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.sincos.

// OGCG-LABEL: @test_late_defined_builtin_sincos
// OGCG: call void @__builtin_sincos(double noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.sincos.

_Bool test_late_defined_builtin_coro_done(void *h) {
  return __builtin_coro_done(h);
}

// CIR-LABEL: @test_late_defined_builtin_coro_done
// CIR: call {{(zeroext )?}}i1 @__builtin_coro_done(ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_done
// OGCG: call {{(zeroext )?}}i1 @__builtin_coro_done(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

_Bool test_late_defined_builtin_coro_alloc(void) {
  return __builtin_coro_alloc();
}

// CIR-LABEL: @test_late_defined_builtin_coro_alloc
// CIR: call {{(zeroext )?}}i1 @__builtin_coro_alloc()
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_alloc
// OGCG: call {{(zeroext )?}}i1 @__builtin_coro_alloc()
// OGCG-NOT: @llvm.coro.

long long test_late_defined_builtin_coro_size(void) {
  return __builtin_coro_size();
}

// CIR-LABEL: @test_late_defined_builtin_coro_size
// CIR: call i64 @__builtin_coro_size()
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_size
// OGCG: call i64 @__builtin_coro_size()
// OGCG-NOT: @llvm.coro.

long long test_late_defined_builtin_coro_align(void) {
  return __builtin_coro_align();
}

// CIR-LABEL: @test_late_defined_builtin_coro_align
// CIR: call i64 @__builtin_coro_align()
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_align
// OGCG: call i64 @__builtin_coro_align()
// OGCG-NOT: @llvm.coro.

void *test_late_defined_builtin_coro_noop(void) {
  return __builtin_coro_noop();
}

// CIR-LABEL: @test_late_defined_builtin_coro_noop
// CIR: call ptr @__builtin_coro_noop()
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_noop
// OGCG: call ptr @__builtin_coro_noop()
// OGCG-NOT: @llvm.coro.

void *test_late_defined_builtin_coro_frame(void) {
  return __builtin_coro_frame();
}

// CIR-LABEL: @test_late_defined_builtin_coro_frame
// CIR: call ptr @__builtin_coro_frame()
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_frame
// OGCG: call ptr @__builtin_coro_frame()
// OGCG-NOT: @llvm.coro.

void *test_late_defined_builtin_coro_begin(void *mem) {
  return __builtin_coro_begin(mem);
}

// CIR-LABEL: @test_late_defined_builtin_coro_begin
// CIR: call ptr @__builtin_coro_begin(ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_begin
// OGCG: call ptr @__builtin_coro_begin(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

void *test_late_defined_builtin_coro_free(void *h) {
  return __builtin_coro_free(h);
}

// CIR-LABEL: @test_late_defined_builtin_coro_free
// CIR: call ptr @__builtin_coro_free(ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_free
// OGCG: call ptr @__builtin_coro_free(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

void test_late_defined_builtin_coro_end(void *h, _Bool unwind) {
  __builtin_coro_end(h, unwind);
}

// CIR-LABEL: @test_late_defined_builtin_coro_end
// CIR: call void @__builtin_coro_end(ptr noundef %{{.*}}, i1 noundef {{(zeroext )?}}%{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_end
// OGCG: call void @__builtin_coro_end(ptr noundef %{{.*}}, i1 noundef {{(zeroext )?}}%{{.*}})
// OGCG-NOT: @llvm.coro.

void *test_late_defined_builtin_coro_promise(void *h, int align, _Bool from) {
  return __builtin_coro_promise(h, align, from);
}

// CIR-LABEL: @test_late_defined_builtin_coro_promise
// CIR: call ptr @__builtin_coro_promise(ptr noundef %{{.*}}, i32 noundef %{{.*}}, i1 noundef {{(zeroext )?}}%{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_promise
// OGCG: call ptr @__builtin_coro_promise(ptr noundef %{{.*}}, i32 noundef %{{.*}}, i1 noundef {{(zeroext )?}}%{{.*}})
// OGCG-NOT: @llvm.coro.

int test_late_defined_builtin_coro_suspend(void *h, int final) {
  return __builtin_coro_suspend(h, final);
}

// CIR-LABEL: @test_late_defined_builtin_coro_suspend
// CIR: call i32 @__builtin_coro_suspend(ptr noundef %{{.*}}, i32 noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_suspend
// OGCG: call i32 @__builtin_coro_suspend(ptr noundef %{{.*}}, i32 noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

int test_late_defined_builtin_coro_id(int align, void *promise, void *coroaddr,
                                      void *fnaddrs) {
  return __builtin_coro_id(align, promise, coroaddr, fnaddrs);
}

// CIR-LABEL: @test_late_defined_builtin_coro_id
// CIR: call i32 @__builtin_coro_id(i32 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_id
// OGCG: call i32 @__builtin_coro_id(i32 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

int test_late_defined_builtin_coro_save(void *h) {
  return __builtin_coro_save(h);
}

// CIR-LABEL: @test_late_defined_builtin_coro_save
// CIR: call i32 @__builtin_coro_save(ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_save
// OGCG: call i32 @__builtin_coro_save(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

void test_late_defined_builtin_coro_destroy(void *h) {
  __builtin_coro_destroy(h);
}

// CIR-LABEL: @test_late_defined_builtin_coro_destroy
// CIR: call void @__builtin_coro_destroy(ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_late_defined_builtin_coro_destroy
// OGCG: call void @__builtin_coro_destroy(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

_Bool test_late_defined_builtin_coro_await_suspend_bool(void *awaiter,
                                                        void *frame,
                                                        void *wrapper) {
  return __builtin_coro_await_suspend_bool(awaiter, frame, wrapper);
}

// CIR-LABEL: @test_late_defined_builtin_coro_await_suspend_bool
// CIR: call {{(zeroext )?}}i1 @__builtin_coro_await_suspend_bool(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.await.suspend.

// OGCG-LABEL: @test_late_defined_builtin_coro_await_suspend_bool
// OGCG: call {{(zeroext )?}}i1 @__builtin_coro_await_suspend_bool(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.await.suspend.

void *test_late_defined_builtin_coro_await_suspend_handle(void *awaiter,
                                                          void *frame,
                                                          void *wrapper) {
  return __builtin_coro_await_suspend_handle(awaiter, frame, wrapper);
}

// CIR-LABEL: @test_late_defined_builtin_coro_await_suspend_handle
// CIR: call ptr @__builtin_coro_await_suspend_handle(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.await.suspend.

// OGCG-LABEL: @test_late_defined_builtin_coro_await_suspend_handle
// OGCG: call ptr @__builtin_coro_await_suspend_handle(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.await.suspend.

void test_late_defined_builtin_coro_await_suspend_void(void *awaiter,
                                                        void *frame,
                                                        void *wrapper) {
  __builtin_coro_await_suspend_void(awaiter, frame, wrapper);
}

// CIR-LABEL: @test_late_defined_builtin_coro_await_suspend_void
// CIR: call void @__builtin_coro_await_suspend_void(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.await.suspend.

// OGCG-LABEL: @test_late_defined_builtin_coro_await_suspend_void
// OGCG: call void @__builtin_coro_await_suspend_void(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.await.suspend.

static double __builtin_modf(double x, double *iptr) {
  *iptr = x;
  return x;
}

static double __builtin_frexp(double x, int *eptr) {
  *eptr = 7;
  return x;
}

static void __builtin_coro_resume(void *h) { (void)h; }

static double modf(double x, double *iptr) {
  *iptr = -x;
  return x;
}

static void sincos(double x, double *sinp, double *cosp) {
  *sinp = x;
  *cosp = -x;
}

static void __builtin_sincos(double x, double *sinp, double *cosp) {
  *sinp = -x;
  *cosp = x;
}

static void __builtin_sincospi(double x, double *sinp, double *cosp) {
  *sinp = x;
  *cosp = x;
}

static _Bool __builtin_coro_done(void *h) { return h != 0; }
static _Bool __builtin_coro_alloc(void) { return 0; }
static long long __builtin_coro_size(void) { return 42; }
static long long __builtin_coro_align(void) { return 64; }
static void *__builtin_coro_noop(void) { return (void *)0; }
static void *__builtin_coro_frame(void) { return (void *)0; }
static void *__builtin_coro_begin(void *mem) { return mem; }
static void *__builtin_coro_free(void *h) { return h; }
static void __builtin_coro_end(void *h, _Bool unwind) {
  (void)h;
  (void)unwind;
}
static void *__builtin_coro_promise(void *h, int align, _Bool from) {
  return h ? h : (void *)(long)(align + from);
}
static int __builtin_coro_suspend(void *h, int final) {
  return (h != 0) + final;
}
static int __builtin_coro_id(int align, void *promise, void *coroaddr,
                             void *fnaddrs) {
  return align + (promise != 0) + (coroaddr != 0) + (fnaddrs != 0);
}
static int __builtin_coro_save(void *h) { return h != 0; }
static void __builtin_coro_destroy(void *h) { (void)h; }

static _Bool __builtin_coro_await_suspend_bool(void *awaiter, void *frame,
                                               void *wrapper) {
  return awaiter || frame || wrapper;
}

static void *__builtin_coro_await_suspend_handle(void *awaiter, void *frame,
                                                 void *wrapper) {
  return awaiter ? awaiter : (frame ? frame : wrapper);
}

static void __builtin_coro_await_suspend_void(void *awaiter, void *frame,
                                              void *wrapper) {
  (void)awaiter;
  (void)frame;
  (void)wrapper;
}
