// RUN: %clang_cc1 -fsyntax-only -fpattern-matching -ast-dump %s | FileCheck %s

void test_match0([[maybe_unused]] int a, [[maybe_unused]] int b) {
  3 match {
    _ => 0;
  };
}