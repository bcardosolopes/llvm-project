// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
//
// Verify member function pointer returning struct: the member function pointer
// ({i64, i64}) is decomposed into two i64 scalar arguments.
// Previously XFAIL due to member pointer not being decomposed per SysV ABI.
// Now correctly passes member function pointers as (i64, i64) and data
// member pointers as (i64).

struct Result {
    int value;
};

struct Worker {
    Result compute() { return {42}; }
};

Result call_worker(Worker* w, Result (Worker::*ptr)()) {
    return (w->*ptr)();
}

int test() {
    Worker w;
    return call_worker(&w, &Worker::compute).value;
}

// CHECK: define {{.*}} i32 @_Z11call_workerP6WorkerMS_F6ResultvE(ptr noundef %{{.*}}, i64 %{{.*}}, i64 %{{.*}})
