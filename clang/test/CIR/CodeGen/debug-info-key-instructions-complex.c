// RUN: %clang_cc1 -triple x86_64-linux-gnu -gkey-instructions -gno-column-info -x c++ %s -debug-info-kind=line-tables-only -emit-llvm -fclangir -o - \
// RUN: | FileCheck %s --check-prefixes=CHECK,CHECK-CXX
// RUN: %clang_cc1 -triple x86_64-linux-gnu -gkey-instructions -gno-column-info -x c %s -debug-info-kind=line-tables-only -emit-llvm -fclangir -o - \
// RUN: | FileCheck %s --check-prefixes=CHECK,CHECK-C

_Complex float ci;
float f;
void test() {
// CHECK: load float, ptr @ci{{.*}}, !dbg [[G1R2:!.*]]
// CHECK: load float, ptr getelementptr inbounds nuw {{.*}}@ci{{.*}}, !dbg [[G1R2]]
// CHECK: store float {{.*}}, ptr %{{.*}}, !dbg [[G1R1:!.*]]
// CHECK: store float {{.*}}, ptr %{{.*}}, !dbg [[G1R1]]
  _Complex float lc = ci;

// CHECK: load float, ptr @ci{{.*}}, !dbg [[G2R2:!.*]]
// CHECK: load float, ptr getelementptr inbounds nuw {{.*}}@ci{{.*}}, !dbg [[G2R2]]
// CHECK: store float {{.*}}, ptr @ci{{.*}}, !dbg [[G2R1:!.*]]
// CHECK: store float {{.*}}, ptr getelementptr inbounds nuw {{.*}}@ci{{.*}}, !dbg [[G2R1]]
  ci = ci;

// CHECK: fadd float {{.*}}, {{.*}}, !dbg [[G3R2:!.*]]
// CHECK: fadd float {{.*}}, {{.*}}, !dbg [[G3R2]]
// CHECK: store float {{.*}}, ptr @ci{{.*}}, !dbg [[G3R1:!.*]]
// CHECK: store float {{.*}}, ptr getelementptr inbounds nuw {{.*}}@ci{{.*}}, !dbg [[G3R1]]
  ci += ci;

// CHECK: fsub float {{.*}}, {{.*}}, !dbg [[G4R2:!.*]]
// CHECK: fsub float {{.*}}, {{.*}}, !dbg [[G4R2]]
// CHECK: store float {{.*}}, ptr @ci{{.*}}, !dbg [[G4R1:!.*]]
// CHECK: store float {{.*}}, ptr getelementptr inbounds nuw {{.*}}@ci{{.*}}, !dbg [[G4R1]]
  ci -= ci;

// CHECK: fcmp une float {{.*}}, {{.*}}, !dbg [[G5R2:!.*]]
// CHECK: store float {{.*}}, ptr @ci{{.*}}, !dbg [[G5R1:!.*]]
// CHECK: store float {{.*}}, ptr getelementptr inbounds nuw {{.*}}@ci{{.*}}, !dbg [[G5R1]]
  ci *= ci;

// CHECK: load float, ptr %{{.*}}, align 4, !dbg [[G6R2:!.*]]
// CHECK: load float, ptr %{{.*}}, align 4, !dbg [[G6R2]]
// CHECK: store float {{.*}}, ptr @ci{{.*}}, !dbg [[G6R1:!.*]]
// CHECK: store float {{.*}}, ptr getelementptr inbounds nuw {{.*}}@ci{{.*}}, !dbg [[G6R1]]
  ci /= ci;

// CHECK: fadd float {{.*}}, {{.*}}, !dbg [[G7R2:!.*]]
// CHECK: store float {{.*}}, ptr getelementptr inbounds nuw {{.*}}@ci{{.*}}, !dbg [[G7R1:!.*]]
  __imag ci = __imag ci + __imag ci;

#ifndef __cplusplus
// CHECK-C: fadd float {{.*}}, {{.*}}, !dbg [[G8R2:!.*]]
// CHECK-C: store float {{.*}}, ptr @f{{.*}}, !dbg [[G8R1:!.*]]
  f += ci;

// CHECK-C: fsub float {{.*}}, {{.*}}, !dbg [[G9R2:!.*]]
// CHECK-C: store float {{.*}}, ptr @f{{.*}}, !dbg [[G9R1:!.*]]
  f -= ci;

// CHECK-C: load float, ptr %{{.*}}, align 4, !dbg [[G10R2:!.*]]
// CHECK-C: store float {{.*}}, ptr @f{{.*}}, !dbg [[G10R1:!.*]]
  f /= ci;

// CHECK-C: fmul float {{.*}}, {{.*}}, !dbg [[G11R2:!.*]]
// CHECK-C: store float {{.*}}, ptr @f{{.*}}, !dbg [[G11R1:!.*]]
  f *= ci;
#endif

// CHECK: ret{{.*}}, !dbg [[RET:!.*]]
}

// CHECK-DAG: [[G1R2]] = !DILocation({{.*}}, atomGroup: 1, atomRank: 2)
// CHECK-DAG: [[G1R1]] = !DILocation({{.*}}, atomGroup: 1, atomRank: 1)
// CHECK-DAG: [[G2R2]] = !DILocation({{.*}}, atomGroup: 2, atomRank: 2)
// CHECK-DAG: [[G2R1]] = !DILocation({{.*}}, atomGroup: 2, atomRank: 1)
// CHECK-DAG: [[G3R2]] = !DILocation({{.*}}, atomGroup: 3, atomRank: 2)
// CHECK-DAG: [[G3R1]] = !DILocation({{.*}}, atomGroup: 3, atomRank: 1)
// CHECK-DAG: [[G4R2]] = !DILocation({{.*}}, atomGroup: 4, atomRank: 2)
// CHECK-DAG: [[G4R1]] = !DILocation({{.*}}, atomGroup: 4, atomRank: 1)
// CHECK-DAG: [[G5R2]] = !DILocation({{.*}}, atomGroup: 5, atomRank: 2)
// CHECK-DAG: [[G5R1]] = !DILocation({{.*}}, atomGroup: 5, atomRank: 1)
// CHECK-DAG: [[G6R2]] = !DILocation({{.*}}, atomGroup: 6, atomRank: 2)
// CHECK-DAG: [[G6R1]] = !DILocation({{.*}}, atomGroup: 6, atomRank: 1)
// CHECK-DAG: [[G7R2]] = !DILocation({{.*}}, atomGroup: 7, atomRank: 2)
// CHECK-DAG: [[G7R1]] = !DILocation({{.*}}, atomGroup: 7, atomRank: 1)
// CHECK-C-DAG: [[G8R2]] = !DILocation({{.*}}, atomGroup: 8, atomRank: 2)
// CHECK-C-DAG: [[G8R1]] = !DILocation({{.*}}, atomGroup: 8, atomRank: 1)
// CHECK-C-DAG: [[G9R2]] = !DILocation({{.*}}, atomGroup: 9, atomRank: 2)
// CHECK-C-DAG: [[G9R1]] = !DILocation({{.*}}, atomGroup: 9, atomRank: 1)
// CHECK-C-DAG: [[G10R2]] = !DILocation({{.*}}, atomGroup: 10, atomRank: 2)
// CHECK-C-DAG: [[G10R1]] = !DILocation({{.*}}, atomGroup: 10, atomRank: 1)
// CHECK-C-DAG: [[G11R2]] = !DILocation({{.*}}, atomGroup: 11, atomRank: 2)
// CHECK-C-DAG: [[G11R1]] = !DILocation({{.*}}, atomGroup: 11, atomRank: 1)
// CHECK-C-DAG: [[RET]] = !DILocation({{.*}}, atomGroup: 12, atomRank: 1)
// CHECK-CXX-DAG: [[RET]] = !DILocation({{.*}}, atomGroup: 8, atomRank: 1)
