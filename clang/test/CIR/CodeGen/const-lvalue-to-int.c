// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

typedef unsigned long uintptr_t;

// Test that null pointer cast to integer produces correct constant.
uintptr_t null_as_int = (uintptr_t)(void*)0;

// CHECK: @null_as_int = {{(dso_local )?}}global i64 0
