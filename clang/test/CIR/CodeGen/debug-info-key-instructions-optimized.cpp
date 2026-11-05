// RUN: %clang -target x86_64-unknown-linux-gnu -g -O2 -S -emit-llvm -fclangir \
// RUN:   -gkey-instructions %s -o - | FileCheck %s

int g;

void f() {}
int h() { return g = 1; }

// CHECK-LABEL: define dso_local void @_Z1fv()
// CHECK: ret void, !dbg [[F_RET:![0-9]+]]

// CHECK-LABEL: define dso_local noundef i32 @_Z1hv()
// CHECK: store i32 1, ptr @g, align 4, !dbg [[H_STORE:![0-9]+]]
// CHECK: ret i32 1, !dbg [[H_RET:![0-9]+]]

// CHECK: [[F:!.*]] = distinct !DISubprogram(name: "f"
// CHECK-SAME: flags: DIFlagPrototyped | DIFlagAllCallsDescribed
// CHECK-SAME: spFlags: DISPFlagDefinition | DISPFlagOptimized
// CHECK-SAME: keyInstructions: true
// CHECK: [[F_RET]] = !DILocation(line: 6, column: 11, scope: [[F]], atomGroup: 1, atomRank: 1)

// CHECK: [[H:!.*]] = distinct !DISubprogram(name: "h"
// CHECK-SAME: flags: DIFlagPrototyped | DIFlagAllCallsDescribed
// CHECK-SAME: spFlags: DISPFlagDefinition | DISPFlagOptimized
// CHECK-SAME: keyInstructions: true
// CHECK: [[H_STORE]] = !DILocation(line: 7, column: 20, scope: [[H]], atomGroup: 2, atomRank: 1)
// CHECK: [[H_RET]] = !DILocation(line: 7, column: 11, scope: [[H]], atomGroup: 1, atomRank: 1)
