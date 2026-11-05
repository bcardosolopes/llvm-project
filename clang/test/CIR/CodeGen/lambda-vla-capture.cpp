// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -Wno-vla-cxx-extension -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -Wno-vla-cxx-extension -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test that lambdas can capture VLA types. The lambda closure stores the
// VLA's size (not a pointer to the VLA), and the lambda body restores
// the VLA size from the capture field into vlaSizeMap.

typedef unsigned long size_t;
extern "C" size_t strlen(const char *);

void use(const char *, int);

// Closure type has a u64 field at index 0 for the VLA size.
// CHECK: !rec_anon2E0 = !cir.record<class "anon.0" {!u64i, !cir.ptr<!s8i>, !cir.ptr<!s32i>}>

// Lambda body loads the VLA size from the closure field 0.
// CHECK-LABEL: cir.func{{.*}} @_ZZ4testPKcENK3
// CHECK:   cir.get_member %{{.*}}[0] {name = ""}
// CHECK:   cir.load{{.*}} !cir.ptr<!u64i>, !u64i

// Caller stores the VLA size into the closure field 0.
// CHECK-LABEL: cir.func{{.*}} @_Z4testPKc
// CHECK:   cir.get_member %{{.*}}[0] {name = ""}
// CHECK:   cir.store{{.*}} !u64i, !cir.ptr<!u64i>

// LLVM: %class.anon.0 = type { i64, ptr, ptr }

// Lambda body loads VLA size from field 0.
// LLVM-LABEL: define internal void @"_ZZ4testPKcENK3
// LLVM:   getelementptr inbounds nuw %class.anon.0, ptr %{{.*}}, i32 0, i32 0
// LLVM:   load i64

// Caller stores VLA size into closure field 0.
// LLVM-LABEL: define{{.*}} void @_Z4testPKc
// LLVM:   %[[VLA:.*]] = alloca i8, i64 %{{.*}}, align 16
// LLVM:   getelementptr inbounds nuw %class.anon.0, ptr %{{.*}}, i32 0, i32 0
// LLVM:   store i64
void test(const char *tag) {
  int n = strlen(tag);
  char buf[n + 1];

  auto log = [&]() {
    use(buf, n);
  };

  log();
}
