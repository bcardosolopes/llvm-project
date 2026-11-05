// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Previously crashed with "C++ constructors static var context" and
// "C++ destructors static var context" NYI errors.

struct Counter {
    int id;
    Counter() {
        static int count = 0;
        id = count++;
    }
};

struct Tracker {
    ~Tracker() {
        static int dtor_calls = 0;
        dtor_calls++;
    }
};

void test() {
    Counter c;
    Tracker t;
}

// CHECK-DAG: @_ZZN7CounterC1EvE5count = {{.*}}global i32 0
// CHECK-DAG: @_ZZN7TrackerD1EvE10dtor_calls = {{.*}}global i32 0
// CHECK: define {{.*}}@_Z4testv()
