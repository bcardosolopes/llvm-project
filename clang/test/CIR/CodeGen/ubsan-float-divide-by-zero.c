// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s \
// RUN:   -o %t.cir.ll -disable-llvm-passes -fsanitize=float-divide-by-zero
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s \
// RUN:   -o %t.ogcg.ll -disable-llvm-passes -fsanitize=float-divide-by-zero
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s \
// RUN:   -o %t.trap.ll -disable-llvm-passes \
// RUN:   -fsanitize=float-divide-by-zero -fsanitize-trap=float-divide-by-zero
// RUN: FileCheck --check-prefix=TRAP --input-file=%t.trap.ll %s

float div_float(float a, float b) {
  return a / b;
}

double div_double(double a, double b) {
  return a / b;
}

// LLVM-LABEL: define dso_local float @div_float
// LLVM: fcmp une float
// LLVM: call void @__ubsan_handle_divrem_overflow_abort({{.*}})
// LLVM: fdiv float

// OGCG-LABEL: define dso_local float @div_float
// OGCG: fcmp une float
// OGCG: call void @__ubsan_handle_divrem_overflow_abort({{.*}})
// OGCG: fdiv float

// LLVM-LABEL: define dso_local double @div_double
// LLVM: fcmp une double
// LLVM: call void @__ubsan_handle_divrem_overflow_abort({{.*}})
// LLVM: fdiv double

// OGCG-LABEL: define dso_local double @div_double
// OGCG: fcmp une double
// OGCG: call void @__ubsan_handle_divrem_overflow_abort({{.*}})
// OGCG: fdiv double

// TRAP: call void @llvm.ubsantrap(i8 3)
