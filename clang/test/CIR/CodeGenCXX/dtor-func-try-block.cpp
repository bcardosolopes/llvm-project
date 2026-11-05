// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -std=c++20 -fcxx-exceptions -fexceptions %s -o %t.ll
// Destructor function-try-block with return in catch handler.
// Previously crashed with EHScopeStack::find assertion.
// This is now fixed.
//
// From clang/test/CodeGenCXX/exceptions.cpp (test10::A).

struct A { ~A(); };
void cleanup();

A::~A() try { cleanup(); } catch (...) { return; }
