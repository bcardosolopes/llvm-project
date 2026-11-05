// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 \
// RUN:   -gkey-instructions -debug-info-kind=line-tables-only \
// RUN:   -emit-llvm -fclangir -fexceptions -fcxx-exceptions %s -o - \
// RUN:   | FileCheck %s --implicit-check-not=atomGroup --implicit-check-not=atomRank

struct B {
  float y;
};

class Cls {
public:
  int x = 1;
  B b = {5.f};
};

void fun() {
  Cls c;
}

// CHECK-LABEL: define linkonce_odr void @_ZN3ClsC2Ev(
// CHECK: store i32 1, ptr %{{.*}}, align 4, !dbg [[G1R1:!.*]]
// CHECK: store float 5.000000e+00, ptr %{{.*}}, align 4, !dbg [[G2R1:!.*]]
// CHECK: ret void, !dbg [[RET:!.*]]

// CHECK-DAG: [[G1R1]] = !DILocation(line: 12{{.*}}, scope: ![[#]], atomGroup: 1, atomRank: 1)
// CHECK-DAG: [[G2R1]] = !DILocation(line: 13{{.*}}, scope: ![[#]], atomGroup: 2, atomRank: 1)
// CHECK-DAG: [[RET]] = !DILocation(line: 10, column: 7, scope: ![[#]], atomGroup: 3, atomRank: 1)
