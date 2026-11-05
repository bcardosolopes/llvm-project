// RUN: %clang_cc1 -fclangir -emit-llvm -std=c++20 %s -o /dev/null
// RUN: %clang_cc1 -fclangir -emit-cir -std=c++20 %s -o /dev/null

// Test that member function pointers with incomplete argument types
// don't crash during target lowering. Previously hit an assertion in
// getCIRRecordLayout when classifying an incomplete record type for
// the X86-64 ABI.

struct fake_tuple;
struct connection {
    void bar(fake_tuple);
};
void (connection::*a)(fake_tuple) = &connection::bar;
void f() {
  void (connection::*b)(fake_tuple) = &connection::bar;
}
