// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm -fclangir-call-conv-lowering %s -o /dev/null

// Keep many declaration-only symbols with no users so call-conv lowering can
// skip no-op declaration rewrites efficiently.
extern int dead_decl_0(int);
extern int dead_decl_1(int);
extern int dead_decl_2(int);
extern int dead_decl_3(int);
extern int dead_decl_4(int);
extern int dead_decl_5(int);
extern int dead_decl_6(int);
extern int dead_decl_7(int);
extern int dead_decl_8(int);
extern int dead_decl_9(int);
extern int dead_decl_10(int);
extern int dead_decl_11(int);

// Keep one declaration with non-call symbol users (member-function pointer
// constants). This must remain conservative and never be skipped incorrectly.
struct fake_tuple;
struct connection {
  void bar(fake_tuple);
};

void (connection::*kSink)(fake_tuple) = &connection::bar;
void use_mptr() { void (connection::*tmp)(fake_tuple) = &connection::bar; }
