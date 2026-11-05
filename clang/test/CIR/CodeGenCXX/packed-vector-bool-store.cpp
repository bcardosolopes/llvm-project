// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -fclangir -emit-llvm -std=c++20 %s -o /dev/null

// Packed vector<bool> store/load now implemented via integer bitcast.
typedef bool bool4 __attribute__((ext_vector_type(4)));

void test(bool4 *p, bool4 v) {
  *p = v;
}
