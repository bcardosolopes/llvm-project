// RUN: %clang_cc1 -triple aarch64-apple-darwin -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.x86.ll
// RUN: FileCheck --input-file=%t.x86.ll %s --check-prefix=X86

// Verify that swiftcall ABI properly decomposes aggregates into scalar
// register types, matching OGCG behavior.

#define SWIFTCALL __attribute__((swiftcall))
#define CONTEXT __attribute__((swift_context))
#define ERROR __attribute__((swift_error_result))

// Empty struct should be eliminated.
typedef struct { } struct_empty;
// CHECK-LABEL: define swiftcc void @take_empty()
// X86-LABEL: define dso_local swiftcc void @take_empty()
SWIFTCALL void take_empty(struct_empty s) {}

// Simple struct decomposed into individual fields.
typedef struct { int x; float y; } struct_simple;
// CHECK-LABEL: define swiftcc void @take_simple(i32 %0, float %1)
// X86-LABEL: define dso_local swiftcc void @take_simple(i32 %0, float %1)
SWIFTCALL void take_simple(struct_simple s) {}

// CHECK-LABEL: define swiftcc { i32, float } @return_simple()
// X86-LABEL: define dso_local swiftcc { i32, float } @return_simple()
SWIFTCALL struct_simple return_simple(void) {
  struct_simple s = {1, 2.0f};
  return s;
}

// Padded struct: char + padding + int → merged into i64.
typedef struct { char a; int b; } struct_padded;
// CHECK-LABEL: define swiftcc void @take_padded(i64 %0)
SWIFTCALL void take_padded(struct_padded s) {}

// CHECK: define swiftcc i64 @return_padded()
SWIFTCALL struct_padded return_padded(void) {
  struct_padded s = {'a', 42};
  return s;
}

// Large struct → multiple register-sized pieces.
typedef struct { int a; int b; int c; int d; int e; } struct_big;
// CHECK-LABEL: define swiftcc void @take_big(i64 %0, i64 %1, i32 %2)
SWIFTCALL void take_big(struct_big s) {}

// Mixed int/float preserves float lanes.
typedef struct { double d; float f; } struct_mixed;
// CHECK-LABEL: define swiftcc void @take_mixed(double %0, float %1)
SWIFTCALL void take_mixed(struct_mixed s) {}

// Three-element struct with mixed types.
typedef struct { int x; float y; float z; } struct_3;
// CHECK-LABEL: define swiftcc void @take_3(i32 %0, float %1, float %2)
SWIFTCALL void take_3(struct_3 s) {}

// Packed struct with vector member: the vector's alloc size (16) exceeds its
// store size (12), so the next field's offset must account for tail padding.
// This also exercises target-specific vector legalization: AArch64 splits the
// 96-bit vector into a legal 64-bit vector plus a scalar lane, while x86_64
// keeps the 3-lane vector intact.
typedef int int3 __attribute__((ext_vector_type(3)));
typedef struct {
  int3 vect;
  unsigned long long val;
} __attribute__((packed)) struct_packed_vec;
// CHECK-LABEL: define swiftcc void @take_packed_vec(<2 x i32> %0, i32 %1, i64 %2)
// X86-LABEL: define dso_local swiftcc void @take_packed_vec(<3 x i32> %0, i64 %1)
SWIFTCALL void take_packed_vec(struct_packed_vec s) {}
// CHECK: define swiftcc { <2 x i32>, i32, i64 } @return_packed_vec()
// X86: define dso_local swiftcc { <3 x i32>, i64 } @return_packed_vec()
SWIFTCALL struct_packed_vec return_packed_vec(void) {
  struct_packed_vec x = {};
  return x;
}

// Context parameter gets swiftself.
// CHECK-LABEL: define swiftcc void @take_context(i32 noundef %0, ptr noundef swiftself %1)
SWIFTCALL void take_context(int x, CONTEXT void *self) {}
