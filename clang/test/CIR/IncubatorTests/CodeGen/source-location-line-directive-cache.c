// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

#line 200 "virt-loc-cache.c"
int repeated_return_loc(int x) {
  return x;
}

// CHECK-LABEL: cir.func{{.*}} @repeated_return_loc
// CHECK: cir.store %arg0, %0 : !s32i, !cir.ptr<!s32i> loc(#loc[[STORE_ARG:[0-9]+]])
// CHECK: cir.load{{.*}} loc(#loc[[LOAD:[0-9]+]])
// CHECK: cir.return{{.*}} loc(#loc[[RET:[0-9]+]])
// CHECK-DAG: #loc[[STORE_ARG]] = loc("virt-loc-cache.c":200:32)
// CHECK-DAG: #loc[[LOAD]] = loc("virt-loc-cache.c":201:10)
// CHECK-DAG: #loc[[RET]] = loc("virt-loc-cache.c":201:3)
