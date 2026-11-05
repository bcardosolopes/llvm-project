// Regression test: on 32-bit-size_t targets (i386, 32-bit ARM), the coroutine
// intrinsics llvm.coro.size / llvm.coro.align are overloaded on their result
// type and must be emitted with the target's size_t width (i32), matching the
// CIR builtin's declared size_t result. Hardcoding i64 in the CIR->LLVM
// lowering left an unmaterialized `builtin.unrealized_conversion_cast i64 to
// i32` (feeding operator new / the frame allocator), which crashed translation
// with "LLVM Translation failed for operation: builtin.unrealized_conversion_cast".

// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -std=c23 -fclangir \
// RUN:   -fcoroutines -emit-llvm -disable-llvm-passes -o %t.i386.ll %s
// RUN: FileCheck --input-file=%t.i386.ll %s -check-prefix=LLVM32
// RUN: %clang_cc1 -triple armv7-unknown-linux-gnueabi -std=c23 -fclangir \
// RUN:   -fcoroutines -emit-llvm -disable-llvm-passes -o %t.arm.ll %s
// RUN: FileCheck --input-file=%t.arm.ll %s -check-prefix=LLVM32

void *myAlloc(unsigned long);

// The size/align intrinsics must be the i32 overload, and no unrealized cast
// may survive to the emitted module.
// LLVM32-LABEL: define dso_local void @f(
// LLVM32:         call i32 @llvm.coro.size.i32()
// LLVM32:         call i32 @llvm.coro.align.i32()
// LLVM32-NOT:     unrealized_conversion_cast
void f(int n) {
  int promise;
  __builtin_coro_id(32, &promise, 0, 0);
  __builtin_coro_alloc();
  __builtin_coro_begin(myAlloc(__builtin_coro_size() + __builtin_coro_align()));
  __builtin_coro_resume(__builtin_coro_frame());
  __builtin_coro_free(__builtin_coro_frame());
  __builtin_coro_end(__builtin_coro_frame(), 0);
  __builtin_coro_suspend(1);
}
