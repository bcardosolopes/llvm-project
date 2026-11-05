// RUN: %clang_cc1 -std=c++23 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++23 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// C++23 static operator() - previously crashed with "reference binding
// to unmaterialized r-value" assertion in emitCallArg. Now fixed.

struct Functor {
    static int operator()(int x, int y) {
        return x + y;
    }
};

void test() {
    Functor f;
    f(1, 2);
}

// CHECK: define {{.*}}@_Z4testv
// CHECK: call {{.*}}@_ZN7FunctorclEii(i32 {{.*}}1, i32 {{.*}}2)

// OGCG: define {{.*}}@_Z4testv
// OGCG: call {{.*}}@_ZN7FunctorclEii(i32 {{.*}}1, i32 {{.*}}2)
