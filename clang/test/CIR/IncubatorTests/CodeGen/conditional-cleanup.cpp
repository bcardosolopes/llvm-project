// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -Wno-unused-value -mconstructor-aliases -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fcxx-exceptions -fexceptions -mconstructor-aliases -fclangir -emit-cir %s -o %t.eh.cir
// RUN: FileCheck --check-prefix=CIR_EH --input-file=%t.eh.cir %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fcxx-exceptions -fexceptions -mconstructor-aliases -fclangir -emit-cir-flat -fno-clangir-call-conv-lowering %s -o %t.eh.flat.cir
// RUN: FileCheck --check-prefix=CIR_FLAT_EH --input-file=%t.eh.flat.cir %s

typedef __typeof(sizeof(0)) size_t;

// Declare the reserved global placement new.
void *operator new(size_t, void*);

namespace test7 {
  struct A { A(); ~A(); };
  struct B {
    static void *operator new(size_t size) throw();
    B(const A&, B*);
    ~B();
  };

  B *test() {
    return new B(A(), new B(A(), 0));
  }
}

// CIR-DAG: ![[A:.*]] = !cir.record<struct "test7::A" padded {!u8i}
// CIR-DAG: ![[B:.*]] = !cir.record<struct "test7::B" padded {!u8i}

// CIR-LABEL: _ZN5test74testEv
// CIR:   %[[RET_VAL:.*]] = cir.alloca !cir.ptr<![[B]]>, !cir.ptr<!cir.ptr<![[B]]>>, ["__retval"] {alignment = 8 : i64}
// CIR:   cir.scope {
// CIR:     %[[TMP_A0:.*]] = cir.alloca ![[A]], !cir.ptr<![[A]]>, ["ref.tmp0"] {alignment = 1 : i64}
// CIR:     %[[TMP_A1:.*]] = cir.alloca ![[A]], !cir.ptr<![[A]]>, ["ref.tmp1"] {alignment = 1 : i64}

// CIR:     %[[NULL_CHECK0:.*]] = cir.cmp(ne
// CIR:     %[[PTR_B0:.*]] = cir.cast bitcast
// CIR:     cir.if %[[NULL_CHECK0]] {

// Ctor call: @test7::A::A()
// CIR:       cir.call @_ZN5test71AC1Ev(%[[TMP_A0]]) : (!cir.ptr<![[A]]>) -> ()

// CIR:       %[[NULL_CHECK1:.*]] = cir.cmp(ne
// CIR:       %[[PTR_B1:.*]] = cir.cast bitcast
// CIR:       cir.if %[[NULL_CHECK1]] {

// Ctor call: @test7::A::A()
// CIR:         cir.call @_ZN5test71AC1Ev(%[[TMP_A1]]) : (!cir.ptr<![[A]]>) -> ()
// Ctor call: @test7::B::B()
// CIR:         cir.call @_ZN5test71BC1ERKNS_1AEPS0_(%[[PTR_B1]], %[[TMP_A1]], {{.*}}) : (!cir.ptr<![[B]]>, !cir.ptr<![[A]]>, !cir.ptr<![[B]]>) -> ()
// CIR:       }

// Ctor call: @test7::B::B()
// CIR:       cir.call @_ZN5test71BC1ERKNS_1AEPS0_(%[[PTR_B0]], %[[TMP_A0]], %[[PTR_B1]]) : (!cir.ptr<![[B]]>, !cir.ptr<![[A]]>, !cir.ptr<![[B]]>) -> ()
// CIR:     }
// CIR:     cir.store{{.*}} %[[PTR_B0]], %[[RET_VAL]] : !cir.ptr<![[B]]>, !cir.ptr<!cir.ptr<![[B]]>>
// Dtor calls: @test7::A::~A() - unconditional in non-EH mode
// CIR:     cir.call @_ZN5test71AD1Ev(%[[TMP_A1]]) nothrow : (!cir.ptr<![[A]]>) -> ()
// CIR:     cir.call @_ZN5test71AD1Ev(%[[TMP_A0]]) nothrow : (!cir.ptr<![[A]]>) -> ()
// CIR:   }
// CIR:   cir.return
// CIR: }

// CIR_EH-DAG: ![[A:.*]] = !cir.record<struct "test7::A" padded {!u8i}
// CIR_EH-DAG: ![[B:.*]] = !cir.record<struct "test7::B" padded {!u8i}

// CIR_EH-LABEL: @_ZN5test74testEv
// CIR_EH:   %[[RET_VAL:.*]] = cir.alloca !cir.ptr<![[B]]>, !cir.ptr<!cir.ptr<![[B]]>>, ["__retval"] {alignment = 8 : i64}
// CIR_EH:   cir.scope {
// CIR_EH:     %[[TMP_A0:.*]] = cir.alloca ![[A]], !cir.ptr<![[A]]>, ["ref.tmp0"] {alignment = 1 : i64}
// CIR_EH:     %[[TMP_A1:.*]] = cir.alloca ![[A]], !cir.ptr<![[A]]>, ["ref.tmp1"] {alignment = 1 : i64}
// CIR_EH:     %[[SIZE0:.*]] = cir.const #cir.int<1> : !u64i
// CIR_EH:     %[[MEM0:.*]] = cir.call @_ZN5test71BnwEm(%[[SIZE0]]) nothrow
// CIR_EH:     %[[NULL_CHECK0:.*]] = cir.cmp(ne
// CIR_EH:     %[[PTR_B0:.*]] = cir.cast bitcast %[[MEM0]]
// CIR_EH:     cir.if %[[NULL_CHECK0]] {
// First A::A() call with operator delete cleanup for outer new B
// CIR_EH:       cir.try synthetic cleanup {
// CIR_EH:         cir.call exception @_ZN5test71AC1Ev(%[[TMP_A0]]) : (!cir.ptr<![[A]]>) -> () cleanup {
// CIR_EH:           cir.call @_ZdlPvm(%[[MEM0]], %[[SIZE0]])
// CIR_EH:           cir.yield
// CIR_EH:         }
// CIR_EH:         cir.yield
// CIR_EH:       } unwind {
// CIR_EH:         cir.resume
// CIR_EH:       }
// CIR_EH:       %[[SIZE1:.*]] = cir.const #cir.int<1> : !u64i
// CIR_EH:       %[[MEM1:.*]] = cir.call @_ZN5test71BnwEm(%[[SIZE1]]) nothrow
// CIR_EH:       %[[NULL_CHECK1:.*]] = cir.cmp(ne
// CIR_EH:       %[[PTR_B1:.*]] = cir.cast bitcast %[[MEM1]]
// CIR_EH:       cir.if %[[NULL_CHECK1]] {
// Second A::A() call with operator delete cleanup for inner new B
// CIR_EH:         cir.try synthetic cleanup {
// CIR_EH:           cir.call exception @_ZN5test71AC1Ev(%[[TMP_A1]]) : (!cir.ptr<![[A]]>) -> () cleanup {
// CIR_EH:             cir.call @_ZdlPvm(%[[MEM1]], %[[SIZE1]])
// CIR_EH:             cir.yield
// CIR_EH:           }
// CIR_EH:           cir.yield
// CIR_EH:         } unwind {
// CIR_EH:           cir.resume
// CIR_EH:         }
// Inner B::B() call with A destructor cleanup
// CIR_EH:         cir.try synthetic cleanup {
// CIR_EH:           cir.call exception @_ZN5test71BC1ERKNS_1AEPS0_(%[[PTR_B1]], %[[TMP_A1]], {{.*}}) : (!cir.ptr<![[B]]>, !cir.ptr<![[A]]>, !cir.ptr<![[B]]>) -> () cleanup {
// CIR_EH:             cir.call @_ZN5test71AD1Ev(%[[TMP_A1]])
// CIR_EH:             cir.yield
// CIR_EH:           }
// CIR_EH:           cir.yield
// CIR_EH:         } unwind {
// CIR_EH:           cir.resume
// CIR_EH:         }
// CIR_EH:       }
// Outer B::B() call with A destructor cleanup
// CIR_EH:       cir.try synthetic cleanup {
// CIR_EH:         cir.call exception @_ZN5test71BC1ERKNS_1AEPS0_(%[[PTR_B0]], %[[TMP_A0]], %[[PTR_B1]]) : (!cir.ptr<![[B]]>, !cir.ptr<![[A]]>, !cir.ptr<![[B]]>) -> () cleanup {
// CIR_EH:           cir.call @_ZN5test71AD1Ev(%[[TMP_A1]])
// CIR_EH:           cir.yield
// CIR_EH:         }
// CIR_EH:         cir.yield
// CIR_EH:       } unwind {
// CIR_EH:         cir.resume
// CIR_EH:       }
// CIR_EH:     }
// CIR_EH:     cir.store{{.*}} %[[PTR_B0]], %[[RET_VAL]]
// Unconditional dtor calls in normal flow
// CIR_EH:     cir.call @_ZN5test71AD1Ev(%[[TMP_A1]]) nothrow
// CIR_EH:     cir.call @_ZN5test71AD1Ev(%[[TMP_A0]]) nothrow
// CIR_EH:   }
// CIR_EH:   cir.return

// Nothing special, just test it passes!
// CIR_FLAT_EH-LABEL: @_ZN5test74testEv
