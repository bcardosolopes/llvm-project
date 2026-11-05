// RUN: %clang_cc1 -std=c++20 -triple aarch64-none-linux-android21 -Wno-unused-value -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++20 -fexceptions -fcxx-exceptions -triple aarch64-none-linux-android21 -Wno-unused-value -fclangir -emit-cir %s -o %t.eh.cir
// RUN: FileCheck --check-prefix=CIR_EH --input-file=%t.eh.cir %s

struct Vec {
  Vec();
  Vec(Vec&&);
  ~Vec();
};

struct S1 {
  Vec v;
};

// CIR-DAG: ![[VecType:.*]] = !cir.record<struct "Vec" padded {!u8i}>
// CIR-DAG: ![[S1:.*]] = !cir.record<struct "S1" padded {!u8i}>

// CIR_EH-DAG: ![[VecType:.*]] = !cir.record<struct "Vec" padded {!u8i}>
// CIR_EH-DAG: ![[S1:.*]] = !cir.record<struct "S1" padded {!u8i}>

template <int I>
void make1() {
  Vec v;
  S1((Vec&&) v);
// CIR: cir.func {{.*}} @_Z5make1ILi0EEvv()
// CIR:   %[[VEC:.*]] = cir.alloca ![[VecType]], !cir.ptr<![[VecType]]>
// CIR:   cir.call @_ZN3VecC1Ev(%[[VEC]]) : (!cir.ptr<![[VecType]]>)
// CIR:   cir.scope {
// CIR:     %[[AGG_TMP:.*]] = cir.alloca ![[S1]], !cir.ptr<![[S1]]>, ["agg.tmp.ensured"]
// CIR:     %[[FIELD:.*]] = cir.cast bitcast %[[AGG_TMP]] : !cir.ptr<![[S1]]> -> !cir.ptr<![[VecType]]>
// CIR:     cir.call @_ZN3VecC1EOS_(%[[FIELD]], %[[VEC]]) : (!cir.ptr<![[VecType]]>, !cir.ptr<![[VecType]]>) -> ()
// CIR:     cir.call @_ZN2S1D1Ev(%[[AGG_TMP]]) {{.*}}: (!cir.ptr<![[S1]]>) -> ()
// CIR:   }
// CIR:   cir.call @_ZN3VecD1Ev(%[[VEC]]) {{.*}}: (!cir.ptr<![[VecType]]>) -> ()
// CIR:   cir.return

// CIR_EH: cir.func {{.*}} @_Z5make1ILi0EEvv()
// CIR_EH:  %[[VEC:.*]] = cir.alloca ![[VecType]], !cir.ptr<![[VecType]]>, ["v", init]

// Cleanup active flag alloca for field destructor
// CIR_EH:  cir.alloca !cir.bool, !cir.ptr<!cir.bool>, ["cleanup.cond"]

// Construct v
// CIR_EH:  cir.call @_ZN3VecC1Ev(%[[VEC]]) : (!cir.ptr<![[VecType]]>) -> ()

// Everything guarded by v's destructor goes in one cir.cleanup.scope, so the
// move ctor unwinds to the scope's single shared landing pad.
// CIR_EH:  cir.cleanup.scope {
// CIR_EH:    cir.scope {
// CIR_EH:      %[[AGG:.*]] = cir.alloca ![[S1]], !cir.ptr<![[S1]]>, ["agg.tmp.ensured"]
// CIR_EH:      %[[FIELD:.*]] = cir.cast bitcast %[[AGG]] : !cir.ptr<![[S1]]> -> !cir.ptr<![[VecType]]>
// CIR_EH:      cir.call exception @_ZN3VecC1EOS_{{.*}}
// CIR_EH:      cir.call @_ZN2S1D1Ev(%[[AGG]]) {{.*}}: (!cir.ptr<![[S1]]>) -> ()
// CIR_EH:    }
// CIR_EH:    cir.yield
// CIR_EH:  } cleanup {
// CIR_EH:    cir.call @_ZN3VecD1Ev(%[[VEC]]) {{.*}}: (!cir.ptr<![[VecType]]>) -> ()
// CIR_EH:    cir.yield
// CIR_EH:  }

// Destroy v after successful scope
// CIR_EH:  cir.call @_ZN3VecD1Ev(%[[VEC]]) {{.*}}: (!cir.ptr<![[VecType]]>) -> ()
// CIR_EH:  cir.return
}

void foo() {
  make1<0>();
}
