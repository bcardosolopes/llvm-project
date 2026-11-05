// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c23 -fdefer-ts -emit-llvm -disable-llvm-passes -O1 %s -o - | FileCheck %s --check-prefix=OGCG

void g(void);
void x(int);

// OGCG-LABEL: define{{.*}} void @simple_break_zero_arg(
// OGCG: %cleanup.dest.slot = alloca i32, align 4
// OGCG: call void @g()
// OGCG-NOT: call void @g()
// OGCG: switch i32 %cleanup.dest
void simple_break_zero_arg(int n) {
  for (;;) {
    _Defer g();
    if (n)
      break;
    n = 1;
  }
}

// OGCG-LABEL: define{{.*}} i32 @distinct_defer_return_one_arg(
// OGCG: call void @x(i32 noundef 9)
// OGCG: call void @x(i32 noundef 9)
// OGCG-NOT: call void @x(i32 noundef 9)
int distinct_defer_return_one_arg(int n) {
  _Defer x(9);
  _Defer x(9);
  if (n)
    return 1;
  return 2;
}

// OGCG-LABEL: define{{.*}} i32 @simple_return_one_arg(
// OGCG: call void @x(i32 noundef 9)
// OGCG-NOT: call void @x(i32 noundef 9)
// OGCG: ret i32
int simple_return_one_arg(int n) {
  _Defer x(9);
  if (n)
    return 1;
  return 2;
}
