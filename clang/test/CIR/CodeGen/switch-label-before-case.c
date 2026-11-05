// RUN: %clang_cc1 -std=c17 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:     -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -std=c17 -triple x86_64-unknown-linux-gnu \
// RUN:     -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// A regular label before a case label must not cause the break to be
// placed outside the case region.

void label_before_case(int s) {
  switch (s) {
  L:
  case 1:
    s = 1;
    if (s)
      break;
  }
}

// CHECK-LABEL: define {{.*}} @label_before_case(
// CHECK:   switch i32 %{{.*}}, label %{{.*}} [
// CHECK:     i32 1, label %[[CASE:.*]]
// CHECK:   ]
// CHECK: [[CASE]]:
// CHECK:   store i32 1, ptr %{{.*}}
// CHECK:   br i1 %{{.*}}, label %[[BREAK:.*]], label %[[CONT:.*]]
// CHECK: [[BREAK]]:
// CHECK:   br label %[[END:.*]]

// OGCG-LABEL: define {{.*}} @label_before_case(
// OGCG:   switch i32 %{{.*}}, label %{{.*}} [
// OGCG:     i32 1, label %[[CASE:.*]]
// OGCG:   ]
// OGCG: [[CASE]]:
// OGCG:   store i32 1, ptr %{{.*}}
// OGCG:   br i1 %{{.*}}, label %[[BREAK:.*]], label %[[CONT:.*]]
// OGCG: [[BREAK]]:
// OGCG:   br label %[[END:.*]]

// Multiple labels before a case.
void multi_label_before_case(int s) {
  switch (s) {
  L1:
  L2:
  case 1:
    s = 1;
    if (s)
      break;
  }
}

// CHECK-LABEL: define {{.*}} @multi_label_before_case(
// CHECK:   switch i32 %{{.*}}, label %{{.*}} [
// CHECK:     i32 1, label %[[CASE:.*]]
// CHECK:   ]
// CHECK: [[CASE]]:
// CHECK:   store i32 1, ptr %{{.*}}

// OGCG-LABEL: define {{.*}} @multi_label_before_case(
// OGCG:   switch i32 %{{.*}}, label %{{.*}} [
// OGCG:     i32 1, label %[[CASE:.*]]
// OGCG:   ]
// OGCG: [[CASE]]:
// OGCG:   store i32 1, ptr %{{.*}}

// Label before default.
void label_before_default(int s) {
  switch (s) {
  L:
  default:
    s = 1;
    if (s)
      break;
  }
}

// CHECK-LABEL: define {{.*}} @label_before_default(
// CHECK:   switch i32 %{{.*}}, label %[[DEFAULT:.*]] [
// CHECK:   ]
// CHECK: [[DEFAULT]]:
// CHECK:   store i32 1, ptr %{{.*}}
// CHECK:   br i1 %{{.*}}, label %[[BREAK:.*]], label %[[CONT:.*]]
// CHECK: [[BREAK]]:
// CHECK:   br label %[[END:.*]]

// OGCG-LABEL: define {{.*}} @label_before_default(
// OGCG:   switch i32 %{{.*}}, label %[[DEFAULT:.*]] [
// OGCG:   ]
// OGCG: [[DEFAULT]]:
// OGCG:   store i32 1, ptr %{{.*}}
// OGCG:   br i1 %{{.*}}, label %[[BREAK:.*]], label %[[CONT:.*]]
// OGCG: [[BREAK]]:
// OGCG:   br label %[[END:.*]]

// Minimal reproducer: label with null stmt before case.
// The label's dead block had a cir.yield terminator that became invalid
// after switch body inlining during FlattenCFG.
int label_null_stmt_before_case() {
  switch (0) {
  a:;
  case 0:;
  }
  return 0;
}

// CHECK-LABEL: define {{.*}} @label_null_stmt_before_case(
// CHECK:   ret i32 {{.*}}

// OGCG-LABEL: define {{.*}} @label_null_stmt_before_case(
// OGCG:   ret i32 0

// variant: goto targeting a label before a case.
int label_goto_before_case() {
  switch (0) {
  a:;
  case 0:
    goto a;
  }
  return 0;
}

// The goto a; must create an infinite loop between the label and the case.
// Both must NOT fall through to ret i32 0 from within the loop.
// CHECK-LABEL: define {{.*}} @label_goto_before_case(
// CHECK:   switch i32 0
// CHECK-NOT: ret i32 0
// CHECK:   br label
// CHECK:   br label

// OGCG-LABEL: define {{.*}} @label_goto_before_case(
// OGCG:   switch i32 0
// OGCG-NOT: ret i32 0
// OGCG:   br label
// OGCG:   br label

// Label with break before a case. The dead label block has a
// cir.break terminator that must be replaced during switch flattening.
void label_break_before_case(int s) {
  switch (s) {
  L:
    break;
  case 1:
    s = 1;
  }
}

// CHECK-LABEL: define {{.*}} @label_break_before_case(
// CHECK:   switch i32 %{{.*}}, label %{{.*}} [
// CHECK:     i32 1, label %[[CASE:.*]]
// CHECK:   ]

// OGCG-LABEL: define {{.*}} @label_break_before_case(
// OGCG:   switch i32 %{{.*}}, label %{{.*}} [
// OGCG:     i32 1, label %[[CASE:.*]]
// OGCG:   ]
