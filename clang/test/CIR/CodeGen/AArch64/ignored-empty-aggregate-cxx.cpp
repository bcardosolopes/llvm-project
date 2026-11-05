// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -std=c++20 -fclangir -emit-mlir=cir-flat -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -std=c++20 -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -std=c++20 -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// REQUIRES: aarch64-registered-target

struct Empty {};

struct SuperEmpty {
  int arr[0];
};

struct SortOfEmpty {
  SuperEmpty e;
};

extern "C" Empty empty_ret() {
  Empty e;
  return e;
}

extern "C" SortOfEmpty sort_of_empty_ret() {
  SortOfEmpty e;
  return e;
}

extern "C" Empty empty_arg_variadic(int a, ...) {
  __builtin_va_list vl;
  __builtin_va_start(vl, a);
  Empty b = __builtin_va_arg(vl, Empty);
  __builtin_va_end(vl);
  return b;
}

extern "C" SuperEmpty super_empty_arg_variadic(int a, ...) {
  __builtin_va_list vl;
  __builtin_va_start(vl, a);
  SuperEmpty b = __builtin_va_arg(vl, SuperEmpty);
  __builtin_va_end(vl);
  return b;
}

extern "C" SortOfEmpty sort_of_empty_arg_variadic(int a, ...) {
  __builtin_va_list vl;
  __builtin_va_start(vl, a);
  SortOfEmpty b = __builtin_va_arg(vl, SortOfEmpty);
  __builtin_va_end(vl);
  return b;
}

// CIR-LABEL: cir.func{{.*}} @empty_ret()
// CIR-NOT: ["__retval"]
// CIR-NEXT: cir.return

// CIR-LABEL: cir.func{{.*}} @sort_of_empty_ret()
// CIR-NOT: ["__retval"]
// CIR-NEXT: cir.return

// CIR-LABEL: cir.func{{.*}} @empty_arg_variadic(
// CIR-NOT: ["__retval"]
// CIR-NOT: ["vaarg.tmp"]
// CIR: cir.va_start
// CIR: cir.va_end
// CIR-NEXT: cir.return

// CIR-LABEL: cir.func{{.*}} @super_empty_arg_variadic(
// CIR-NOT: ["__retval"]
// CIR-NOT: ["vaarg.tmp"]
// CIR: cir.va_start
// CIR: cir.va_end
// CIR-NEXT: cir.return

// CIR-LABEL: cir.func{{.*}} @sort_of_empty_arg_variadic(
// CIR-NOT: ["__retval"]
// CIR-NOT: ["vaarg.tmp"]
// CIR: cir.va_start
// CIR: cir.va_end
// CIR-NEXT: cir.return

// LLVM-LABEL: define dso_local void @empty_ret()
// LLVM: ret void
// OGCG-LABEL: define dso_local void @empty_ret()
// OGCG: ret void

// LLVM-LABEL: define dso_local void @sort_of_empty_ret()
// LLVM: ret void
// OGCG-LABEL: define dso_local void @sort_of_empty_ret()
// OGCG: ret void

// LLVM-LABEL: define dso_local void @empty_arg_variadic(i32 noundef {{%.*}}, ...)
// LLVM-NOT: __retval
// LLVM-NOT: vaarg.tmp
// LLVM-NOT: alloca %struct.Empty
// LLVM-NOT: load %struct.Empty
// LLVM-NOT: store %struct.Empty
// LLVM: call void @llvm.va_start.p0
// LLVM: call void @llvm.va_end.p0
// LLVM-NEXT: ret void
// OGCG-LABEL: define dso_local void @empty_arg_variadic(i32 noundef {{%.*}}, ...)
// OGCG: call void @llvm.va_start.p0
// OGCG: call void @llvm.va_end.p0
// OGCG-NEXT: ret void

// LLVM-LABEL: define dso_local void @super_empty_arg_variadic(i32 noundef {{%.*}}, ...)
// LLVM-NOT: __retval
// LLVM-NOT: vaarg.tmp
// LLVM-NOT: alloca %struct.SuperEmpty
// LLVM-NOT: load %struct.SuperEmpty
// LLVM-NOT: store %struct.SuperEmpty
// LLVM: call void @llvm.va_start.p0
// LLVM: call void @llvm.va_end.p0
// LLVM-NEXT: ret void
// OGCG-LABEL: define dso_local void @super_empty_arg_variadic(i32 noundef {{%.*}}, ...)
// OGCG: call void @llvm.va_start.p0
// OGCG: call void @llvm.va_end.p0
// OGCG-NEXT: ret void

// LLVM-LABEL: define dso_local void @sort_of_empty_arg_variadic(i32 noundef {{%.*}}, ...)
// LLVM-NOT: __retval
// LLVM-NOT: vaarg.tmp
// LLVM-NOT: alloca %struct.SortOfEmpty
// LLVM-NOT: load %struct.SortOfEmpty
// LLVM-NOT: store %struct.SortOfEmpty
// LLVM: call void @llvm.va_start.p0
// LLVM: call void @llvm.va_end.p0
// LLVM-NEXT: ret void
// OGCG-LABEL: define dso_local void @sort_of_empty_arg_variadic(i32 noundef {{%.*}}, ...)
// OGCG: call void @llvm.va_start.p0
// OGCG: call void @llvm.va_end.p0
// OGCG-NEXT: ret void
