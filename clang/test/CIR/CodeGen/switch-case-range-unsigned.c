// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s

// Regression test: switch case ranges on unsigned long long must use
// unsigned comparisons, not signed.

int check_range(unsigned long long x) {
    switch (x) {
    case 0ULL ... 999999999999999999ULL:
        return 0;
    case 1000000000000000000ULL ... 9999999999999999999ULL:
        return 1;
    default:
        return 2;
    }
}

// CHECK-LABEL: @check_range
// CHECK: sub i64
// CHECK: icmp ule i64
