// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s

#line 300 "virt-range-cache.c"
int repeated_range_use(int x, int y) {
  int z = x + y;
  return z + z;
}

// CHECK-LABEL: cir.func{{.*}} @repeated_range_use
// CHECK: %[[ADD1:.*]] = cir.binop(add, %{{.*}}, %{{.*}}){{.*}} loc(#loc[[FIRST:[0-9]+]])
// CHECK: %[[ADD2:.*]] = cir.binop(add, %{{.*}}, %{{.*}}){{.*}} loc(#loc[[SECOND:[0-9]+]])
// CHECK-DAG: #loc[[B1:[0-9]+]] = loc("virt-range-cache.c":301:11)
// CHECK-DAG: #loc[[E1:[0-9]+]] = loc("virt-range-cache.c":301:15)
// CHECK-DAG: #loc[[FIRST]] = loc(fused[#loc[[B1]], #loc[[E1]]])
// CHECK-DAG: #loc[[B2:[0-9]+]] = loc("virt-range-cache.c":302:10)
// CHECK-DAG: #loc[[E2:[0-9]+]] = loc("virt-range-cache.c":302:14)
// CHECK-DAG: #loc[[SECOND]] = loc(fused[#loc[[B2]], #loc[[E2]]])
