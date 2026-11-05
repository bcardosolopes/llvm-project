// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fexceptions -fcxx-exceptions \
// RUN:   -emit-llvm -fclangir -fprofile-instrument=clang -fcoverage-mapping %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fexceptions -fcxx-exceptions \
// RUN:   -emit-llvm -fclangir -fprofile-instrument=clang -fcoverage-mapping \
// RUN:   -dump-coverage-mapping %s -o /dev/null | FileCheck %s --check-prefix=DUMP

struct E {};

extern "C" int g(int x) {
  try {
    if (x)
      throw E{};
  } catch (const E &) {
    return 1;
  }
  return 0;
}

// CHECK: @__profc_g = private global [4 x i64] zeroinitializer, section "__llvm_prf_cnts"
// CHECK-LABEL: define dso_local i32 @g(i32 noundef %0)
// CHECK: load i64, ptr getelementptr inbounds ([4 x i64], ptr @__profc_g, i32 0, i32 1), align 8
// CHECK: store i64 %{{.*}}, ptr getelementptr inbounds ([4 x i64], ptr @__profc_g, i32 0, i32 1), align 8
// CHECK: landingpad { ptr, i32 }
// CHECK: load i64, ptr getelementptr inbounds ([4 x i64], ptr @__profc_g, i32 0, i32 3), align 8
// CHECK: store i64 %{{.*}}, ptr getelementptr inbounds ([4 x i64], ptr @__profc_g, i32 0, i32 3), align 8

// DUMP-LABEL: g:
// DUMP: File 0{{.*}} = #0
// DUMP: File 0{{.*}} = #2
// DUMP: File 0{{.*}} = #1
