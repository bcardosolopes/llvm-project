// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test that global aggregate initialization with large arrays (>4GB total size)
// doesn't overflow 32-bit size computation in RecordType::computeStructSize.

namespace NearlyZeroInit {
  // Array with partial initialization: 123 elements, only first 3 set.
  // CHECK: cir.global external @_ZN14NearlyZeroInit1aE
  int a[123] = {1, 2, 3};

  // Struct with a huge array member (total size > 4GB).
  // The record type size computation must use 64-bit arithmetic.
  struct B { int n; int arr[1024 * 1024 * 1024 * 2u]; };

  // CHECK: cir.global external @_ZN14NearlyZeroInit1bE
  B b = {1, {2}};
}
