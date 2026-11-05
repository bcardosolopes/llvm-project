// Tests that callback functions whose address is taken get call-graph-section
// type metadata, including local-linkage callbacks that are viable indirect
// targets.

// RUN: %clang_cc1 -triple x86_64-unknown-linux -fexperimental-call-graph-section -fclangir -emit-llvm -o - %s | FileCheck %s

typedef void (*CallbackFn)(int);

// CHECK-LABEL: define internal void @_ZL10myCallbacki(
// CHECK: !type [[F_CALLBACK:![0-9]+]]
static void myCallback(int value) {
  volatile int sink = value;
  (void)sink;
}

int takeCallbackAddress() {
  CallbackFn cb = &myCallback;
  volatile void *addr = (void *)cb;
  (void)addr;
  return 0;
}

// CHECK: [[F_CALLBACK]] = !{i64 0, !"_ZTSFviE.generalized"}
