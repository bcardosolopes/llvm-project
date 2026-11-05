// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 -fshort-enums -fclangir -emit-llvm %s -o - | FileCheck %s

typedef union {
  signed char a;
  signed char b;
} tu_c_t __attribute__((transparent_union));

typedef union {
  unsigned short a;
} tu_us_t __attribute__((transparent_union));

typedef union {
  enum flag { red, yellow, blue } fl;
  enum weekend { sun, sat } b;
} etest_t __attribute__((transparent_union));

// CHECK-LABEL: define{{.*}} void @ftest0(
// CHECK-SAME: i8 noundef signext %{{.*}})
void ftest0(tu_c_t uc) {}

// CHECK-LABEL: define{{.*}} void @ftest2(
// CHECK-SAME: i16 noundef zeroext %{{.*}})
void ftest2(tu_us_t uc) {}

// CHECK-LABEL: define{{.*}} void @ftest4(
// CHECK-SAME: i8 noundef zeroext %{{.*}})
void ftest4(etest_t a) {}

