// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fstrict-vtable-pointers -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fstrict-vtable-pointers -O2 -disable-llvm-passes -emit-cir %s -o %t-o2.cir
// RUN: FileCheck --input-file=%t-o2.cir %s -check-prefix=CIR-O2
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fstrict-vtable-pointers -O1 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Test that -fstrict-vtable-pointers correctly emits cir.invariant_group (for
// launder) and cir.strip_invariant_group (for strip) operations, and that
// they lower to @llvm.launder/strip.invariant.group.

typedef __typeof__(sizeof(0)) size_t;
void *operator new(size_t, void *) throw();

struct Base {
  virtual ~Base();
  int x;
};

struct Derived : Base {
  ~Derived() override;
  int y;
};

// Test 1: Dynamic class field access — strip invariant group.
// CIR-LABEL: @_Z12field_access
// CIR: cir.strip_invariant_group
// CIR: cir.get_member
int field_access(Base *b) {
  return b->x;
}

// Test 2: Pointer comparison — strip both sides.
// CIR-LABEL: @_Z11ptr_compare
// CIR: cir.strip_invariant_group
// CIR: cir.strip_invariant_group
// CIR: cir.cmp
bool ptr_compare(Base *a, Base *b) {
  return a == b;
}

// Test 3: Pointer-to-integer — strip before conversion.
// CIR-LABEL: @_Z10ptr_to_int
// CIR: cir.strip_invariant_group
// CIR: cir.cast ptr_to_int
long ptr_to_int(Base *b) {
  return (long)b;
}

// Test 4: Integer-to-pointer — launder after conversion.
// CIR-LABEL: @_Z10int_to_ptr
// CIR: cir.cast int_to_ptr
// CIR: cir.invariant_group
Base *int_to_ptr(long x) {
  return (Base *)x;
}

// Test 5: Cast dynamic-to-non-dynamic (void*) — strip.
// CIR-LABEL: @_Z18dynamic_cast_strip
// CIR: cir.strip_invariant_group
// CIR: cir.cast bitcast
void *dynamic_cast_strip(Base *b) {
  return (void *)b;
}

// Test 6: Cast non-dynamic-to-dynamic (void* to Base*) — launder.
// CIR-LABEL: @_Z18non_dynamic_to_dyn
// CIR: cir.invariant_group
// CIR: cir.cast bitcast
Base *non_dynamic_to_dyn(void *p) {
  return (Base *)p;
}

// Test 7: Null comparison — no strip needed (null has no dynamic info).
// CIR-LABEL: @_Z12null_compare
// CIR-NOT: cir.strip_invariant_group
// CIR: cir.cmp
bool null_compare(Base *b) {
  return b == nullptr;
}

// Test 8: Vtable assumption loads after constructor calls (requires -O2).
// After a complete constructor call, emit cir.assume that the vtable pointer
// equals the expected vtable address point.
// CIR-O2-LABEL: @_Z14construct_base
// CIR-O2: cir.call @_ZN4BaseC1Ev
// CIR-O2: cir.vtable.get_vptr
// CIR-O2: cir.load
// CIR-O2: cir.cmp(eq,
// CIR-O2: cir.assume
void construct_base() {
  Base b;
}

// Test 9: Placement new launder — launder pointer before construction.
// CIR-LABEL: @_Z13placement_new
// CIR: cir.invariant_group
Base *placement_new(void *p) {
  return new(p) Base();
}

// Test 10: __builtin_launder emits launder.invariant.group.
// CIR-LABEL: @_Z7launder
// CIR: cir.invariant_group
Base *launder(Base *p) {
  return __builtin_launder(p);
}

// Verify LLVM lowering produces the intrinsics.
// LLVM-DAG: @llvm.launder.invariant.group
// LLVM-DAG: @llvm.strip.invariant.group
