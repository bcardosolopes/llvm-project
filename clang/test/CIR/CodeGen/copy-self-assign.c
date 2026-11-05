// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// Test volatile struct self-assignment emits a volatile copy (observable side
// effect, e.g. MMIO), while normal aggregate copies also work correctly.

typedef struct { int x; int y; } Point;

// Volatile self-assignment must emit a volatile cir.copy — the self-copy is
// an observable side effect that must not be elided.
void test_volatile_self_assign(void) {
  volatile Point s;
  s = s;
}

// CHECK-LABEL: @test_volatile_self_assign
// CHECK:         cir.copy %{{.+}} to %{{.+}} volatile {{.*}}: !cir.ptr<!rec_Point>
// CHECK:       }

// LLVM-LABEL: @test_volatile_self_assign
// LLVM:         call void @llvm.memcpy{{.*}}({{.*}}, i1 true)
// LLVM:       }

// Normal aggregate copy should still emit cir.copy.
void test_normal_copy(void) {
  Point a, b;
  a = b;
}

// CHECK-LABEL: @test_normal_copy
// CHECK:         cir.copy %{{.+}} to %{{.+}} {{.*}}: !cir.ptr<!rec_Point>
// CHECK:       }

// LLVM-LABEL: @test_normal_copy
// LLVM:         call void @llvm.memcpy
// LLVM:       }
