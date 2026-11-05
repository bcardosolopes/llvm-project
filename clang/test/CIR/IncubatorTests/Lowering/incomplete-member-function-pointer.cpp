// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that member function pointers to functions with incomplete parameter
// types correctly emit empty parameter list declarations, matching OGCG.

struct fake_tuple;
struct connection {
    void bar(fake_tuple);
};

// The declaration should have no parameters because fake_tuple is incomplete.
// CHECK: declare void @_ZN10connection3barE10fake_tuple()
void (connection::*a)(fake_tuple) = &connection::bar;
