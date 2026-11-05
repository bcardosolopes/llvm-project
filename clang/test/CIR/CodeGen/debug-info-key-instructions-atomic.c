// RUN: %clang_cc1 -triple x86_64-linux-gnu -gkey-instructions -gno-column-info \
// RUN:   -debug-info-kind=line-tables-only -emit-llvm -fclangir %s -o - \
// RUN:   | FileCheck %s

_Atomic(unsigned int) x;
unsigned int y;

void atomic_atoms(void) {
  unsigned int r1 = __c11_atomic_fetch_add(&x, -1, __ATOMIC_RELAXED);
  unsigned int r2 = __c11_atomic_load(&x, __ATOMIC_RELAXED);
  __c11_atomic_store(&x, 2, __ATOMIC_RELAXED);
  int r3 = __atomic_test_and_set(&x, __ATOMIC_RELAXED);
  __atomic_clear(&x, __ATOMIC_RELAXED);
  int r4 = __atomic_compare_exchange(&y, &y, &y, 0, __ATOMIC_RELAXED,
                                     __ATOMIC_RELAXED);
}

// CHECK-LABEL: define dso_local void @atomic_atoms(
// CHECK: %{{.*}} = atomicrmw xchg ptr @x, i8 1 monotonic, align 4, !dbg [[LINE12:!.*]]
// CHECK: %{{.*}} = icmp ne i8 %{{.*}}, 0, !dbg [[G7R3:!.*]]
// CHECK: %{{.*}} = zext i1 %{{.*}} to i8, !dbg [[G7R2:!.*]]
// CHECK: store i8 %{{.*}}, ptr %{{.*}}, align 1, !dbg [[G7R1:!.*]]
// CHECK: %{{.*}} = load i8, ptr %{{.*}}, align 1, !dbg [[G6R4:!.*]]
// CHECK: %{{.*}} = trunc i8 %{{.*}} to i1, !dbg [[G6R3:!.*]]
// CHECK: %{{.*}} = zext i1 %{{.*}} to i32, !dbg [[G6R2:!.*]]
// CHECK: store i32 %{{.*}}, ptr %{{.*}}, align 4, !dbg [[G6R1:!.*]]
// CHECK: %{{.*}} = load i32, ptr @y, align 4, !dbg [[LINE14:!.*]]
// CHECK: %{{.*}} = load i32, ptr @y, align 4, !dbg [[LINE14]]
// CHECK: %{{.*}} = cmpxchg ptr @y, i32 %{{.*}}, i32 %{{.*}} monotonic monotonic, align 4, !dbg [[LINE14]]
// CHECK: %{{.*}} = extractvalue { i32, i1 } %{{.*}}, 0, !dbg [[G10R2:!.*]]
// CHECK: %{{.*}} = extractvalue { i32, i1 } %{{.*}}, 1, !dbg [[G10R3:!.*]]
// CHECK: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !dbg [[LINE14]]
// CHECK: store i32 %{{.*}}, ptr @y, align 4, !dbg [[G10R1:!.*]]
// CHECK: br label %{{.*}}, !dbg [[LINE14]]
// CHECK: %{{.*}} = zext i1 %{{.*}} to i8, !dbg [[G10R2]]
// CHECK: store i8 %{{.*}}, ptr %{{.*}}, align 1, !dbg [[G10R1]]
// CHECK: %{{.*}} = load i8, ptr %{{.*}}, align 1, !dbg [[G9R4:!.*]]
// CHECK: %{{.*}} = trunc i8 %{{.*}} to i1, !dbg [[G9R3:!.*]]
// CHECK: %{{.*}} = zext i1 %{{.*}} to i32, !dbg [[G9R2:!.*]]
// CHECK: store i32 %{{.*}}, ptr %{{.*}}, align 4, !dbg [[G9R1:!.*]]
// CHECK: ret void, !dbg [[RET:!.*]]

// CHECK: [[LINE12]] = !DILocation(line: 12, scope: ![[#]])
// CHECK: [[G7R3]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 7, atomRank: 3)
// CHECK: [[G7R2]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 7, atomRank: 2)
// CHECK: [[G7R1]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 7, atomRank: 1)
// CHECK: [[G6R4]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 6, atomRank: 4)
// CHECK: [[G6R3]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 6, atomRank: 3)
// CHECK: [[G6R2]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 6, atomRank: 2)
// CHECK: [[G6R1]] = !DILocation(line: 12, scope: ![[#]], atomGroup: 6, atomRank: 1)
// CHECK: [[G10R2]] = !DILocation(line: 14, scope: ![[#]], atomGroup: 10, atomRank: 2)
// CHECK: [[G10R3]] = !DILocation(line: 14, scope: ![[#]], atomGroup: 10, atomRank: 3)
// CHECK: [[G10R1]] = !DILocation(line: 14, scope: ![[#]], atomGroup: 10, atomRank: 1)
// CHECK: [[G9R4]] = !DILocation(line: 14, scope: ![[#]], atomGroup: 9, atomRank: 4)
// CHECK: [[G9R3]] = !DILocation(line: 14, scope: ![[#]], atomGroup: 9, atomRank: 3)
// CHECK: [[G9R2]] = !DILocation(line: 14, scope: ![[#]], atomGroup: 9, atomRank: 2)
// CHECK: [[G9R1]] = !DILocation(line: 14, scope: ![[#]], atomGroup: 9, atomRank: 1)
// CHECK: [[RET]] = !DILocation(line: 16, scope: ![[#]], atomGroup: 11, atomRank: 1)
