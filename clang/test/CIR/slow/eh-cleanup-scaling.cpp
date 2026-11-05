// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 \
// RUN:   -fcxx-exceptions -fexceptions -fclangir -emit-cir -o - %s \
// RUN:   | grep -c '_ZN4RAIID1Ev' > %t.cir.count
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 \
// RUN:   -fcxx-exceptions -fexceptions -fclangir -emit-llvm -o - %s \
// RUN:   | grep -c '_ZN4RAIID1Ev' > %t.llvm.count
//
// Raw CIR now avoids cloning identical cleanup regions for the repeated
// same-depth may_throw() calls, but it still materializes the constructor
// prefix cleanups at O(K^2/2).
// K = number of live RAII objects, C = number of potentially-throwing calls.
//
// Same-depth calls in one synthetic cleanup try now share one representative
// cleanup region in raw CIR. FlattenCFG still suffix-shares the progressive
// constructor cleanup prefixes, so the final LLVM IR matches OGCG's O(K)
// unwind cleanup call sites even though raw CIR still pays the constructor
// prefix term before flattening.
//
// For K=20, C=40:
// - raw CIR now emits 231 destructor references
// - final LLVM emits 41 total references (40 call sites + 1 declaration),
//   matching OGCG
//
// RUN: %python -c "import sys; sys.exit(0 if int(open(r'%t.cir.count').read().strip()) < 300 else 1)"
// RUN: %python -c "import sys; sys.exit(0 if int(open(r'%t.llvm.count').read().strip()) == 41 else 1)"

struct RAII { RAII(); ~RAII(); };
void may_throw();

// K=20 RAII objects, C=40 potentially-throwing calls.
// CIR: 231 dtor references before FlattenCFG
// LLVM: 41 dtor references after FlattenCFG
void test_20x40() {
  RAII r1, r2, r3, r4, r5, r6, r7, r8, r9, r10;
  RAII r11, r12, r13, r14, r15, r16, r17, r18, r19, r20;
  may_throw(); may_throw(); may_throw(); may_throw(); may_throw();
  may_throw(); may_throw(); may_throw(); may_throw(); may_throw();
  may_throw(); may_throw(); may_throw(); may_throw(); may_throw();
  may_throw(); may_throw(); may_throw(); may_throw(); may_throw();
  may_throw(); may_throw(); may_throw(); may_throw(); may_throw();
  may_throw(); may_throw(); may_throw(); may_throw(); may_throw();
  may_throw(); may_throw(); may_throw(); may_throw(); may_throw();
  may_throw(); may_throw(); may_throw(); may_throw(); may_throw();
}
