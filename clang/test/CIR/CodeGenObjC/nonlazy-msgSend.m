// RUN: %clang_cc1 -triple x86_64-apple-darwin9 -fobjc-runtime=macosx-fragile-10.5 -emit-llvm -fclangir %s -o - | FileCheck %s

// CHECK-DAG: @OBJC_METH_VAR_NAME_ = private unnamed_addr constant [4 x i8] c"foo\00", section "__TEXT,__cstring,cstring_literals", align 1
// CHECK-DAG: @OBJC_SELECTOR_REFERENCES_ = private externally_initialized global ptr @OBJC_METH_VAR_NAME_, section "__OBJC,__message_refs,literal_pointers,no_dead_strip", align 8
// CHECK-DAG: declare ptr @objc_msgSend(ptr, ptr, ...) [[NLB:#[0-9]+]]
// CHECK-LABEL: define void @f0(
// CHECK: %[[SEL:.*]] = load ptr, ptr @OBJC_SELECTOR_REFERENCES_, align 8
// CHECK: call ptr @objc_msgSend(ptr noundef %{{.*}}, ptr noundef %[[SEL]])
void f0(id x) {
  [x foo];
}

// CHECK: attributes [[NLB]] = { nonlazybind }
// CHECK: !{i32 1, !"Objective-C Version", i32 1}
