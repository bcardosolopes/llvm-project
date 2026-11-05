// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

#line 500 "virt-module-loc-cache.c"
int module_loc_global = 7;
int module_loc_user(void) {
  return module_loc_global;
}

// CHECK-LABEL: module
// CHECK: cir.global{{.*}} @module_loc_global{{.*}} loc(#loc{{[0-9]+}})
// CHECK-DAG: loc("virt-module-loc-cache.c":500:1)
// CHECK-DAG: loc("virt-module-loc-cache.c":500:25)
// CHECK-DAG: loc(fused[#loc{{[0-9]+}}, #loc{{[0-9]+}}])
