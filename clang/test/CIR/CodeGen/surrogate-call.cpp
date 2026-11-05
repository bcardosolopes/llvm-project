// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Surrogate call (struct with conversion operator to function pointer)

int f1(int arg) { return arg; }
int f2(float arg) { return int(arg); }

typedef int (*fp1)(int);
typedef int (*fp2)(float);

struct A {
    operator fp1() { return f1; }
    operator fp2() { return f2; }
};

void test() {
    A a;
    int result = a(1);
}

// Verify the surrogate call goes through the conversion operator and then
// an indirect call through the resulting function pointer.
// CHECK: cir.func {{.*}} @_Z4testv
// CHECK:   cir.call @_ZN1AcvPFiiEEv
// CHECK:   cir.call %{{[0-9]+}}
