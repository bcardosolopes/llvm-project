// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-android -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir

// Test that lambda captures with non-trivial destructors don't crash when
// exceptions are disabled (Android disables exceptions by default).
// The pushEHDestroy call must be guarded by needsEHCleanup.

struct A { ~A(); };

void foo() {
    A a;
    [a]() {};
}

// Just verify it compiles without crashing.
// CHECK: cir.func
