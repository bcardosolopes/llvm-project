// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -w %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -w %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test VLA (variable length array) with non-trivial destructors.
// The destruction loop must iterate in reverse and guard against
// empty arrays.

struct S { ~S(); };

void test(int n) {
    S arr[n];
}

// CIR: cir.func {{.*}}@_Z4testi
// CIR:   %[[END:.*]] = cir.ptr_stride
// CIR:   %[[EMPTY:.*]] = cir.cmp(eq,
// CIR:   cir.if %[[EMPTY]]
// CIR:   } else {
// CIR:     cir.do {
// CIR:       cir.yield
// CIR:     } while {
// CIR:       %[[CUR:.*]] = cir.load
// CIR:       %[[DONE:.*]] = cir.cmp(ne,
// CIR:       cir.condition(%[[DONE]])
// CIR:     }
// CIR:   }

// LLVM-LABEL: @_Z4testi
// LLVM:         %[[END:.*]] = getelementptr inbounds %struct.S, ptr %{{.*}}, i64 %{{.*}}
// LLVM:         %[[ISEMPTY:.*]] = icmp eq ptr %{{.*}}, %[[END]]
// LLVM:         br i1 %[[ISEMPTY]]
// LLVM:         getelementptr inbounds %struct.S, ptr %{{.*}}, i64 -1
// LLVM:         call void @_ZN1SD1Ev
