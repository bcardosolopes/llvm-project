// RUN: %clang_cc1 -std=c++17 -triple aarch64-none-linux-android21 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple aarch64-none-linux-android21 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Test that dynamic-length array construction (new T[n] where n is a runtime
// value and T has a non-trivial constructor) emits a forward pointer-based
// do-while loop guarded by a zero-check.

struct A {
  A();
  ~A();
};

void foo(int n) {
  A *p = new A[n];
  delete[] p;
}

// CIR: cir.func {{.*}} @_Z3fooi
// CIR:   %[[NUM:.*]] = cir.cast integral %{{.*}} : !s32i -> !u64i
//
// Compute end pointer: begin + numElements
// CIR:   %[[BEGIN:.*]] = cir.cast bitcast %{{.*}} : !cir.ptr<!void> -> !cir.ptr<!rec_A>
// CIR:   %[[END:.*]] = cir.ptr_stride inbounds %[[BEGIN]], %[[NUM]] : (!cir.ptr<!rec_A>, !u64i) -> !cir.ptr<!rec_A>
//
// Zero-check: skip loop if array is empty
// CIR:   %[[NOTEMPTY:.*]] = cir.cmp(ne, %[[BEGIN]], %[[END]]) : !cir.ptr<!rec_A>, !cir.bool
// CIR:   cir.if %[[NOTEMPTY]] {
//
// Forward construction do-while loop
// CIR:     cir.do {
// CIR:       %[[CUR:.*]] = cir.load {{.*}} : !cir.ptr<!cir.ptr<!rec_A>>, !cir.ptr<!rec_A>
// CIR:       cir.call @_ZN1AC1Ev(%[[CUR]]) : (!cir.ptr<!rec_A>) -> ()
// CIR:       cir.ptr_stride inbounds %[[CUR]], %{{.*}} : (!cir.ptr<!rec_A>, !s64i) -> !cir.ptr<!rec_A>
// CIR:       cir.yield
// CIR:     } while {
// CIR:       %[[CUR2:.*]] = cir.load {{.*}} : !cir.ptr<!cir.ptr<!rec_A>>, !cir.ptr<!rec_A>
// CIR:       %[[DONE:.*]] = cir.cmp(ne, %[[CUR2]], %[[END]]) : !cir.ptr<!rec_A>, !cir.bool
// CIR:       cir.condition(%[[DONE]])
// CIR:     }
// CIR:   }

// LLVM: define {{.*}} @_Z3fooi
// Compute end pointer using runtime numElements
// LLVM:   %[[ENDPTR:.*]] = getelementptr inbounds %struct.A, ptr %{{.*}}, i64 %{{.*}}
// Zero-check: skip loop if begin == end
// LLVM:   icmp ne ptr %{{.*}}, %[[ENDPTR]]
// LLVM:   br i1
// Do-while condition (textually before body in LLVM IR)
// LLVM:   icmp ne ptr %{{.*}}, %[[ENDPTR]]
// LLVM:   br i1
// Loop body: construct element and advance
// LLVM:   call void @_ZN1AC1Ev(ptr {{[^%]*}}%{{.*}})
// LLVM:   getelementptr inbounds %struct.A, ptr %{{.*}}, i64 1
