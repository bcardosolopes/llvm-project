// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
void func100();
int f(int a, int cond) {
  int b = 1; 
  switch (a) 
    while (1) {
        b++;

        default:
            if (cond)
                return a;

            a = a + b;

        case 2:
            a++;

        case 3:
            continue;

        case 5:
            break;

        case 100:
            func100();
  }

  return a;
}

// CHECK: switch i32 %[[A:.+]], label %[[DEFAULT_BB:.+]] [
// CHECK:   i32 2, label %[[TWO_BB:.+]]
// CHECK:   i32 3, label %[[THREE_BB:.+]]
// CHECK:   i32 5, label %[[FIVE_BB:.+]]
// CHECK:   i32 100, label %[[HUNDRED_BB:.+]]
// CHECK: ]
//
// CHECK: [[LOOP_HEADER:.+]]:
// CHECK:   add nsw i32 %{{.*}}, 1
// CHECK: br label %[[DEFAULT_BB:.+]]
//
// CHECK: [[DEFAULT_BB]]:
// CHECK:   %[[CMP:.+]] = icmp ne i32 %[[COND:.+]], 0
// CHECK:   br i1 %[[CMP]], label %[[IF_TRUE_BB:.+]], label %[[IF_FALSE_BB:.+]]
//
// CHECK: [[IF_TRUE_BB]]:
// After cir-flatten-cfg's unifyRetvalMultiReturns, both return paths converge
// on a single ret-block; the early-return inside the switch stores to the
// shared retval slot rather than emitting its own ret.
// CHECK:   store i32
// CHECK:   br label %{{.+}}
//
// CHECK: [[IF_FALSE_BB]]:
// CHECK:   %[[V1:.+]] = load i32
// CHECK:   %[[V2:.+]] = load i32
// CHECK:   add nsw i32 %[[V1]], %[[V2]]
//
// CHECK: [[TWO_BB]]:
// CHECK:   add nsw i32 %{{.*}}, 1
// CHECK:   br label %[[FALLTHOUGH_BB:.+]]
//
// CHECK: [[FALLTHOUGH_BB]]:
// CHECK:   br label %[[LOOP_HEADER]]
//
// CHECK: [[FIVE_BB]]:
// CHECK:   br label %[[LOOP_EXIT_BB:.+]]
//
// CHECK: [[HUNDRED_BB]]:
// CHECK:   call {{.*}}@func100()
// CHECK:   br label %[[CONTINUE_BB:.+]]
//
// CHECK: [[CONTINUE_BB]]:
// CHECK:  br label %[[LOOP_HEADER]]
//
// CHECK: [[LOOP_EXIT_BB]]:
// CHECK:   br label %[[RET_BB:.+]]
//
// CHECK: [[RET_BB]]:
// CHECK:   ret
