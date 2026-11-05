// RUN: %clang_cc1 -triple aarch64-windows-msvc -ffreestanding -emit-llvm -O0 \
// RUN:   -x c++ -fclangir -o - %s | FileCheck %s

struct S {
  int a[3];
  ~S();
};

S make(S x);

S direct() {
  S x;
  return make(x);
}

using Fn = S (*)(S);

S indirect(Fn fn) {
  S x;
  return fn(x);
}

struct Wrapper {
  S method(S x);
  S caller();
};

S Wrapper::caller() {
  S x;
  return method(x);
}

// CHECK-LABEL: define{{.*}} void @{{.*}}direct{{.*}}(ptr dead_on_unwind inreg noalias writable sret(%struct.S) align 4 %{{[^)]+}})
// CHECK: call void @{{.*}}make{{.*}}(ptr dead_on_unwind inreg writable sret(%struct.S) align 4 %{{[^,]+}}, [2 x i64] %{{.*}})

// CHECK-LABEL: define{{.*}} void @{{.*}}indirect{{.*}}(ptr dead_on_unwind inreg noalias writable sret(%struct.S) align 4 %{{[^,]+}}, ptr noundef %{{[^)]+}})
// CHECK: call void %{{.*}}(ptr dead_on_unwind inreg writable sret(%struct.S) align 4 %{{[^,]+}}, [2 x i64] %{{.*}})

// CHECK-LABEL: define{{.*}} void @{{.*}}caller@Wrapper{{.*}}(ptr noundef nonnull align 1 dereferenceable(1) %{{[^,]+}}, ptr dead_on_unwind inreg noalias writable sret(%struct.S) align 4 %{{[^)]+}})
// CHECK: call void @{{.*}}method@Wrapper{{.*}}(ptr noundef nonnull align 1 dereferenceable(1) %{{.*}}, ptr dead_on_unwind inreg writable sret(%struct.S) align 4 %{{[^,]+}}, [2 x i64] %{{.*}})
