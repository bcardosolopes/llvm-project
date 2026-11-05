// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o /dev/null

// Keep many declaration-only symbols with no users so the pass can skip
// no-op rewrite work for them.
extern int never_used_0(int);
extern long never_used_1(long);
extern void never_used_2();

// Keep one declaration symbol user that is not a direct call/get_global user:
// a member-function-pointer constant. This must still be handled safely.
struct fake_tuple;
struct connection {
  void bar(fake_tuple);
};

void (connection::*a)(fake_tuple) = &connection::bar;
void f() { void (connection::*b)(fake_tuple) = &connection::bar; }
