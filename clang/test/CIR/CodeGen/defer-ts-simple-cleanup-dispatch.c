// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c23 -fdefer-ts -emit-llvm -disable-llvm-passes -O1 -fclangir %s -o - | FileCheck %s --check-prefix=CIR

void g(void);
void x(int);

// CIR-LABEL: define{{.*}} void @simple_break_zero_arg(
// CIR: %cleanup.dest.slot = alloca i32, align 4
// CIR: cleanup.dispatch:
// CIR: call void @g()
// CIR-NOT: call void @g()
// CIR: switch i32 %cleanup.dest
void simple_break_zero_arg(int n) {
  for (;;) {
    _Defer g();
    if (n)
      break;
    n = 1;
  }
}

// CIR-LABEL: define{{.*}} i32 @distinct_defer_return_one_arg(
// CIR: cleanup.dispatch:
// CIR: call void @x(i32 noundef 9)
// CIR-NOT: call void @x(i32 noundef 9)
// CIR: switch i32 %cleanup.dest
int distinct_defer_return_one_arg(int n) {
  _Defer x(9);
  _Defer x(9);
  if (n)
    return 1;
  return 2;
}

// CIR-LABEL: define{{.*}} i32 @simple_return_one_arg(
// CIR: cleanup.dispatch:
// CIR: call void @x(i32 noundef 9)
// CIR-NOT: call void @x(i32 noundef 9)
// CIR: switch i32 %cleanup.dest
int simple_return_one_arg(int n) {
  _Defer x(9);
  if (n)
    return 1;
  return 2;
}
