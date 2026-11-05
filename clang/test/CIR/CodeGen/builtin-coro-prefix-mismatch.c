// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll %s --check-prefix=OGCG

// Guard against prefix-only coroutine builtin interception.
// Unknown __builtin_coro_* names must remain normal calls, matching OGCG.
extern void *__builtin_coro_extra(int);
extern void __builtin_coro_destroy_extra(void *);
extern int __builtin_coro_suspend_extra(void *, int);
extern _Bool __builtin_coro_await_suspend_bool_extra(void *, void *, void *);
extern void *__builtin_coro_noop_extra(void);
extern int __builtin_coro_id_extra(int, void *, void *, void *);
extern long long __builtin_coro_align_extra(void);
extern void *__builtin_coro_await_suspend_handle_extra(void *, void *, void *);
extern void *__builtin_coro_begin_extra(void *, void *);
extern void *__builtin_coro_free_extra(void *, void *);
extern void __builtin_coro_resume_extra(void *);
extern _Bool __builtin_coro_done_extra(void *);
extern _Bool __builtin_coro_alloc_extra(void);
extern void __builtin_coro_end_extra(void *, int);
extern long long __builtin_coro_size_extra(void);
extern void *__builtin_coro_save_extra(void);
extern void *__builtin_coro_promise_extra(void *, long long, _Bool);
extern void __builtin_coro_await_suspend_void_extra(void *, void *, void *);

void *test_builtin_coro_prefix_mismatch(int x) {
  return __builtin_coro_extra(x);
}

// CIR-LABEL: @test_builtin_coro_prefix_mismatch
// CIR: call ptr @__builtin_coro_extra(i32 noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_prefix_mismatch
// OGCG: call ptr @__builtin_coro_extra(i32 noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

void test_builtin_coro_destroy_prefix_mismatch(void *h) {
  __builtin_coro_destroy_extra(h);
}

// CIR-LABEL: @test_builtin_coro_destroy_prefix_mismatch
// CIR: call void @__builtin_coro_destroy_extra(ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_destroy_prefix_mismatch
// OGCG: call void @__builtin_coro_destroy_extra(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

int test_builtin_coro_suspend_prefix_mismatch(void *h, int b) {
  return __builtin_coro_suspend_extra(h, b);
}

// CIR-LABEL: @test_builtin_coro_suspend_prefix_mismatch
// CIR: call i32 @__builtin_coro_suspend_extra(ptr noundef %{{.*}}, i32 noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_suspend_prefix_mismatch
// OGCG: call i32 @__builtin_coro_suspend_extra(ptr noundef %{{.*}}, i32 noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

_Bool test_builtin_coro_await_suspend_bool_prefix_mismatch(void *a, void *b,
                                                            void *c) {
  return __builtin_coro_await_suspend_bool_extra(a, b, c);
}

// CIR-LABEL: @test_builtin_coro_await_suspend_bool_prefix_mismatch
// CIR: call {{(zeroext )?}}i1 @__builtin_coro_await_suspend_bool_extra(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_await_suspend_bool_prefix_mismatch
// OGCG: call {{(zeroext )?}}i1 @__builtin_coro_await_suspend_bool_extra(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

void *test_builtin_coro_noop_prefix_mismatch(void) {
  return __builtin_coro_noop_extra();
}

// CIR-LABEL: @test_builtin_coro_noop_prefix_mismatch
// CIR: call ptr @__builtin_coro_noop_extra()
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_noop_prefix_mismatch
// OGCG: call ptr @__builtin_coro_noop_extra()
// OGCG-NOT: @llvm.coro.

int test_builtin_coro_id_prefix_mismatch(int align, void *promise, void *addr,
                                         void *fn) {
  return __builtin_coro_id_extra(align, promise, addr, fn);
}

// CIR-LABEL: @test_builtin_coro_id_prefix_mismatch
// CIR: call i32 @__builtin_coro_id_extra(i32 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_id_prefix_mismatch
// OGCG: call i32 @__builtin_coro_id_extra(i32 noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

long long test_builtin_coro_align_prefix_mismatch(void) {
  return __builtin_coro_align_extra();
}

// CIR-LABEL: @test_builtin_coro_align_prefix_mismatch
// CIR: call i64 @__builtin_coro_align_extra()
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_align_prefix_mismatch
// OGCG: call i64 @__builtin_coro_align_extra()
// OGCG-NOT: @llvm.coro.

void *test_builtin_coro_await_suspend_handle_prefix_mismatch(void *a, void *b,
                                                              void *c) {
  return __builtin_coro_await_suspend_handle_extra(a, b, c);
}

// CIR-LABEL: @test_builtin_coro_await_suspend_handle_prefix_mismatch
// CIR: call ptr @__builtin_coro_await_suspend_handle_extra(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_await_suspend_handle_prefix_mismatch
// OGCG: call ptr @__builtin_coro_await_suspend_handle_extra(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

void *test_builtin_coro_begin_prefix_mismatch(void *id, void *h) {
  return __builtin_coro_begin_extra(id, h);
}

// CIR-LABEL: @test_builtin_coro_begin_prefix_mismatch
// CIR: call ptr @__builtin_coro_begin_extra(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_begin_prefix_mismatch
// OGCG: call ptr @__builtin_coro_begin_extra(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

void *test_builtin_coro_free_prefix_mismatch(void *id, void *h) {
  return __builtin_coro_free_extra(id, h);
}

// CIR-LABEL: @test_builtin_coro_free_prefix_mismatch
// CIR: call ptr @__builtin_coro_free_extra(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_free_prefix_mismatch
// OGCG: call ptr @__builtin_coro_free_extra(ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

void test_builtin_coro_resume_prefix_mismatch(void *h) {
  __builtin_coro_resume_extra(h);
}

// CIR-LABEL: @test_builtin_coro_resume_prefix_mismatch
// CIR: call void @__builtin_coro_resume_extra(ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_resume_prefix_mismatch
// OGCG: call void @__builtin_coro_resume_extra(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

_Bool test_builtin_coro_done_prefix_mismatch(void *h) {
  return __builtin_coro_done_extra(h);
}

// CIR-LABEL: @test_builtin_coro_done_prefix_mismatch
// CIR: call {{(zeroext )?}}i1 @__builtin_coro_done_extra(ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_done_prefix_mismatch
// OGCG: call {{(zeroext )?}}i1 @__builtin_coro_done_extra(ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

_Bool test_builtin_coro_alloc_prefix_mismatch(void) {
  return __builtin_coro_alloc_extra();
}

// CIR-LABEL: @test_builtin_coro_alloc_prefix_mismatch
// CIR: call {{(zeroext )?}}i1 @__builtin_coro_alloc_extra()
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_alloc_prefix_mismatch
// OGCG: call {{(zeroext )?}}i1 @__builtin_coro_alloc_extra()
// OGCG-NOT: @llvm.coro.

void test_builtin_coro_end_prefix_mismatch(void *h, int unwind) {
  __builtin_coro_end_extra(h, unwind);
}

// CIR-LABEL: @test_builtin_coro_end_prefix_mismatch
// CIR: call void @__builtin_coro_end_extra(ptr noundef %{{.*}}, i32 noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_end_prefix_mismatch
// OGCG: call void @__builtin_coro_end_extra(ptr noundef %{{.*}}, i32 noundef %{{.*}})
// OGCG-NOT: @llvm.coro.

long long test_builtin_coro_size_prefix_mismatch(void) {
  return __builtin_coro_size_extra();
}

// CIR-LABEL: @test_builtin_coro_size_prefix_mismatch
// CIR: call i64 @__builtin_coro_size_extra()
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_size_prefix_mismatch
// OGCG: call i64 @__builtin_coro_size_extra()
// OGCG-NOT: @llvm.coro.

void *test_builtin_coro_save_prefix_mismatch(void) {
  return __builtin_coro_save_extra();
}

// CIR-LABEL: @test_builtin_coro_save_prefix_mismatch
// CIR: call ptr @__builtin_coro_save_extra()
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_save_prefix_mismatch
// OGCG: call ptr @__builtin_coro_save_extra()
// OGCG-NOT: @llvm.coro.

void *test_builtin_coro_promise_prefix_mismatch(void *h, long long align,
                                                 _Bool from) {
  return __builtin_coro_promise_extra(h, align, from);
}

// CIR-LABEL: @test_builtin_coro_promise_prefix_mismatch
// CIR: call ptr @__builtin_coro_promise_extra(ptr noundef %{{.*}}, i64 noundef %{{.*}}, i1 noundef{{( zeroext)?}} %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_promise_prefix_mismatch
// OGCG: call ptr @__builtin_coro_promise_extra(ptr noundef %{{.*}}, i64 noundef %{{.*}}, i1 noundef{{( zeroext)?}} %{{.*}})
// OGCG-NOT: @llvm.coro.

void test_builtin_coro_await_suspend_void_prefix_mismatch(void *a, void *b,
                                                           void *c) {
  __builtin_coro_await_suspend_void_extra(a, b, c);
}

// CIR-LABEL: @test_builtin_coro_await_suspend_void_prefix_mismatch
// CIR: call void @__builtin_coro_await_suspend_void_extra(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// CIR-NOT: @llvm.coro.

// OGCG-LABEL: @test_builtin_coro_await_suspend_void_prefix_mismatch
// OGCG: call void @__builtin_coro_await_suspend_void_extra(ptr noundef %{{.*}}, ptr noundef %{{.*}}, ptr noundef %{{.*}})
// OGCG-NOT: @llvm.coro.
