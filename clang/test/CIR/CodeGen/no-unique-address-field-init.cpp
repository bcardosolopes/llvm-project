// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -std=c++20 -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// Test that [[no_unique_address]] fields whose type has a different base
// subobject layout (e.g., classes with virtual destructors that create tail
// padding) are properly handled during member initialization and field access.
// The record layout stores the base subobject type for potentially-overlapping
// fields, but stores and loads must use the complete type.

struct OpState {
  int value;
  virtual ~OpState() = default;
};

OpState makeOpState(int v);

struct Holder {
  int prefix;
  [[no_unique_address]] OpState op;
  int suffix;

  Holder(int p, int s) : prefix(p), op(makeOpState(p)), suffix(s) {}
};

void test() {
  Holder h(1, 2);
}

// The get_member for the [[no_unique_address]] field returns a pointer to the
// base subobject type, which must be bitcast to the complete type before use.

// CIR: cir.get_member %{{.*}}[2] {name = "op"} : !cir.ptr<!rec_Holder> -> !cir.ptr<!rec_OpState2Ebase>
// CIR: cir.cast bitcast %{{.*}} : !cir.ptr<!rec_OpState2Ebase> -> !cir.ptr<!rec_OpState>
