// RUN: %clang_cc1 -triple x86_64-apple-macosx11.0.0 -fclangir -O0 -emit-llvm %s -o - | FileCheck %s

// __builtin_os_log_format fills a buffer with the os_log wire encoding and calls
// a shared linkonce_odr __os_log_helper_* function that performs the stores. The
// helper name encodes buffer alignment, the summary byte, the arg count, and per
// item (size, descriptor); the runtime os_log decoder depends on this exact
// encoding, so it must match classic CodeGen byte for byte.

void sink(void *);

void test(int i, long l, const char *s, double d) {
  char buf[256];
  sink(__builtin_os_log_format(buf, "x=%d y=%ld s=%{public}s d=%f", i, l, s, d));
}

// Summary byte 2 (has non-scalar items) at offset 0 with buffer alignment 16;
// arg-count byte 4 at offset 1; then per item a descriptor byte + size byte +
// the argument value. The %{public}s item has descriptor 34 (StringKind<<4 |
// IsPublic).
// CHECK-LABEL: define linkonce_odr hidden void @__os_log_helper_16_2_4_4_0_8_0_8_34_8_0(ptr {{.*}}, i32 {{.*}}, i64 {{.*}}, i64 {{.*}}, i64 {{.*}})
// CHECK: store i8 2, ptr %{{.*}}, align 16
// CHECK: store i8 4, ptr %{{.*}}, align 1
// CHECK: store i8 0, ptr %{{.*}}, align 2
// CHECK: store i8 4, ptr %{{.*}}, align 1
// CHECK: store i32 %{{.*}}, ptr %{{.*}}, align 4
// CHECK: store i8 0, ptr %{{.*}}, align 8
// CHECK: store i8 8, ptr %{{.*}}, align 1
// CHECK: store i64 %{{.*}}, ptr %{{.*}}, align 2
// CHECK: store i8 34, ptr %{{.*}}, align 2
// CHECK: store i8 8, ptr %{{.*}}, align 1
// CHECK: store i64 %{{.*}}, ptr %{{.*}}, align 4
// CHECK: store i8 0, ptr %{{.*}}, align 4
// CHECK: store i8 8, ptr %{{.*}}, align 1
// CHECK: store i64 %{{.*}}, ptr %{{.*}}, align 2
// CHECK: ret void

// The call site marshals the four scalar arguments and invokes the helper.
// CHECK-LABEL: @test
// CHECK: call void @__os_log_helper_16_2_4_4_0_8_0_8_34_8_0(ptr {{.*}}, i32 {{.*}}, i64 {{.*}}, i64 {{.*}}, i64 {{.*}})
