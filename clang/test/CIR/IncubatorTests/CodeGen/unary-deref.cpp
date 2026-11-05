// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

struct MyIntPointer {
  int *ptr = nullptr;
  int read() const { return *ptr; }
};

void foo() {
  MyIntPointer p;
  (void)p.read();
}

// CHECK:  cir.func {{.*}} @_ZNK12MyIntPointer4readEv
// CHECK:  %[[THIS:.*]] = cir.load{{.*}} %0
// CHECK:  %[[PTR_FIELD:.*]] = cir.get_member %[[THIS]][0] {name = "ptr"}
// CHECK:  %[[PTR:.*]] = cir.load deref{{.*}} %[[PTR_FIELD]] : !cir.ptr<!cir.ptr<!s32i>>
// CHECK:  %[[VAL:.*]] = cir.load{{.*}} %[[PTR]]
