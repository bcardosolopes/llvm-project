// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Test that zero-element records and int-to-pointer coercion work correctly
// through the ABI lowering path.

struct Empty {};

// Zero-element record passed as argument should compile without crashing.
// CHECK-LABEL: @_Z11takes_empty5Empty
void takes_empty(struct Empty e) {}

// Zero-element record returned should compile without crashing.
// CHECK-LABEL: @_Z13returns_emptyv
struct Empty returns_empty() { return {}; }

struct PtrWrapper {
  void *p;
};

// Int-to-pointer coercion: returning a struct with a single pointer field.
// The ABI may coerce this to/from an integer, requiring proper int-ptr
// conversion in createCoercedStore.
// CHECK-LABEL: @_Z18returns_ptrwrapper
struct PtrWrapper returns_ptrwrapper(void *p) {
  struct PtrWrapper w;
  w.p = p;
  return w;
}
