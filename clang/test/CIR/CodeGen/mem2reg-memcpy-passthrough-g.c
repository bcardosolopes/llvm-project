// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -O2 \
// RUN:     -debug-info-kind=limited -dwarf-version=5 \
// RUN:     -fclangir -emit-llvm %s -o - | FileCheck %s

// Under -g, -fclangir runs mem2reg after the
// CIR-to-LLVM lowering with followPromotableChains=true. The destination of the
// memcpy reaches the promoted `words` alloca through a no-op zero-index
// llvm.getelementptr (the array-to-pointer decay of `words`), a pass-through
// chain. Before the fix, the LLVM-dialect memcpy MemorySlot interface compared
// the memcpy operands to slot.ptr exactly, so storesTo returned false and the
// copy was deleted WITHOUT storing the reaching value back -- `words` kept its
// prior (undef/zero) contents, a silent miscompile. The fix resolves the
// operands through the pass-through chain before comparing, so the reaching
// value (here the argument `in`) survives.
//
// This mirrors classic CodeGen: `f` returns exactly its argument. A regression
// that dropped the memcpy store-back would return undef/0 (or leave a stray
// memcpy), failing these CHECKs.

typedef unsigned long size_t;
extern void *memcpy(void *, const void *, size_t);

unsigned f(unsigned in) {
  unsigned words[1];
  memcpy(words, &in, sizeof(in));
  return words[0];
}

// The reaching value stored by the (removed) memcpy must be propagated: `f`
// returns its argument, with no leftover memcpy.
// CHECK:       define {{.*}}i32 @f(i32 {{.*}} %[[IN:[0-9]+]])
// CHECK-NOT:     memcpy
// CHECK:         ret i32 %[[IN]]
