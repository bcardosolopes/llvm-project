// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir \
// RUN:   -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Regression test: emitAggregateCopy must not emit cir.copy with a void
// pointee type (which has no data-layout size).  When the destination pointer
// is void*, emit cir.libc.memcpy with an explicit byte count instead.
//
// This pattern arises from placement-new through void* storage, e.g. in
// std::allocator::construct.

inline void *operator new(decltype(sizeof(0)), void *p) noexcept { return p; }

struct Pair {
  const char *key;
  int value;
};

void construct(void *dst, const Pair &src) {
  ::new (dst) Pair(src);
}

// Verify no cir.copy uses void pointers (would crash during lowering).
// CIR-LABEL: cir.func {{.*}} @_Z9constructPvRK4Pair
// CIR-NOT:     cir.copy {{.*}} !cir.ptr<!void>
// CIR:         cir.return

// Verify the lowering produces a memcpy with correct size (16 bytes).
// LLVM-LABEL: define {{.*}} @_Z9constructPvRK4Pair
// LLVM:         call void @llvm.memcpy.p0.p0.i64(ptr {{.*}}, ptr {{.*}}, i64 16, i1 false)
