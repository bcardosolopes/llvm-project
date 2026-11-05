// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that delete[] correctly calls element destructors in reverse order
// before freeing memory. Previously, the dynamic element count from the
// array cookie was not used to emit a destructor loop.

static int alive = 0;

struct Item {
    int val;
    Item() : val(0) { alive++; }
    ~Item();
};

// CHECK-LABEL: define{{.*}} void @_Z4testv
// CHECK: call{{.*}} ptr @_Znam
// The delete[] path should:
// 1. Subtract cookie to get allocation pointer
// CHECK: getelementptr{{.*}} i8,{{.*}} i64 -8
// 2. Load element count from cookie
// CHECK: load i64
// 3. Compute end pointer for destruction loop
// CHECK: getelementptr{{.*}} %struct.Item
// 4. Call destructors in a backward loop
// CHECK: call void @_ZN4ItemD1Ev
// 5. Call sized operator delete[] with correct size (element_size * count + cookie)
// CHECK: mul i64
// CHECK: add i64
// CHECK: call void @_ZdaPvm
void test() {
    Item* p = new Item[3];
    delete[] p;
}
