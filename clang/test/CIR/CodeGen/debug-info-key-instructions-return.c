// RUN: %clang_cc1 -triple x86_64-linux-gnu -gkey-instructions -gno-column-info \
// RUN:   -debug-info-kind=line-tables-only -emit-llvm -fclangir %s -o - \
// RUN:   | FileCheck %s

int g;

float multi_return(void) {
  if (g)
    return g;
  return 1;
}

// CHECK-LABEL: define dso_local float @multi_return(
// CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !dbg [[G1R1:!.*]]
// CHECK: store float %{{.*}}, ptr %{{.*}}, align 4, !dbg [[G2R1:!.*]]
// CHECK: br label %{{.*}}, !dbg [[G2R1]]
// CHECK: store float 1.000000e+00, ptr %{{.*}}, align 4, !dbg [[G3R1:!.*]]
// CHECK: br label %{{.*}}, !dbg [[G3R1]]
// CHECK: %{{.*}} = load float, ptr %{{.*}}, align 4, !dbg [[G4R2:!.*]]
// CHECK: ret float %{{.*}}, !dbg [[G4R1:!.*]]

// CHECK: [[G1R1]] = !DILocation({{.*}}atomGroup: 1, atomRank: 1)
// CHECK: [[G2R1]] = !DILocation({{.*}}atomGroup: 2, atomRank: 1)
// CHECK: [[G3R1]] = !DILocation({{.*}}atomGroup: 3, atomRank: 1)
// CHECK: [[G4R2]] = !DILocation({{.*}}atomGroup: 4, atomRank: 2)
// CHECK: [[G4R1]] = !DILocation({{.*}}atomGroup: 4, atomRank: 1)
