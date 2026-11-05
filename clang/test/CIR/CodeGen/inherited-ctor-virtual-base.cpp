// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Test: inherited constructors (using Base::Base) with virtual
// inheritance. The base-object inheriting constructor's definition omits
// forwarded parameters (it only sets up VTT, not the actual base construction).
// CIR must strip the forwarded args from the call so operand count matches.

struct B { B(int); };
struct M : virtual B { using B::B; };
struct D : M { D() : B(1), M(2) {} };
D d;

// The inheriting constructor _ZN1MCI21BEi takes only (this, vtt) — no
// forwarded int parameter — because the base-object variant doesn't
// construct the virtual base B.

// CIR: cir.func {{.*}} @_ZN1MCI21BEi(
// CIR-SAME: %{{.*}}: !cir.ptr<!rec_M>
// CIR-SAME: %{{.*}}: !cir.ptr<!cir.ptr<!void>>
// CIR-SAME: )

// The call in D's constructor passes (this, vtt) — matching the callee.
// CIR: cir.func {{.*}} @_ZN1DC1Ev(
// CIR:   cir.call @_ZN1MCI21BEi(%{{.*}}, %{{.*}}) : (!cir.ptr<!rec_M>, !cir.ptr<!cir.ptr<!void>>) -> ()

// LLVM: define {{.*}} @_ZN1MCI21BEi(ptr {{.*}}, ptr {{.*}})

// In OGCG, the definition takes (this, vtt) but the call passes an extra
// i32 arg the callee ignores (opaque pointers allow this). CIR strips it.
// OGCG: define {{.*}} @_ZN1MCI21BEi(ptr {{.*}}, ptr {{.*}})
