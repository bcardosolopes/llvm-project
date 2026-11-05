// Simple functions
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o -  | FileCheck %s

void empty() { }
// CHECK: cir.func{{.*}} @_Z5emptyv()
// CHECK:   cir.return
// CHECK: }

void voidret() { return; }
// CHECK: cir.func{{.*}} @_Z7voidretv()
// CHECK:   cir.return
// CHECK: }

int intfunc() { return 42; }
// CHECK: cir.func{{.*}} @_Z7intfuncv() -> (!s32i {cir.noundef})
// CHECK:   %0 = cir.const #cir.int<42> : !s32i
// CHECK:   cir.return %0 : !s32i
// CHECK: }

int scopes() {
  {
    {
      return 99;
    }
  }
}
// CHECK: cir.func{{.*}} @_Z6scopesv() -> (!s32i {cir.noundef})
// CHECK:   cir.scope {
// CHECK:     cir.scope {
// CHECK:       %0 = cir.const #cir.int<99> : !s32i
// CHECK:       cir.return %0 : !s32i
// CHECK:     }
// CHECK:   }
// CHECK:   cir.trap
// CHECK: }

long longfunc() { return 42l; }
// CHECK: cir.func{{.*}} @_Z8longfuncv() -> (!s64i {cir.noundef})
// CHECK:   %0 = cir.const #cir.int<42> : !s64i
// CHECK:   cir.return %0 : !s64i
// CHECK: }

unsigned unsignedfunc() { return 42u; }
// CHECK: cir.func{{.*}} @_Z12unsignedfuncv() -> (!u32i {cir.noundef})
// CHECK:   %0 = cir.const #cir.int<42> : !u32i
// CHECK:   cir.return %0 : !u32i
// CHECK: }

unsigned long long ullfunc() { return 42ull; }
// CHECK: cir.func{{.*}} @_Z7ullfuncv() -> (!u64i {cir.noundef})
// CHECK:   %0 = cir.const #cir.int<42> : !u64i
// CHECK:   cir.return %0 : !u64i
// CHECK: }

bool boolfunc() { return true; }
// CHECK: cir.func{{.*}} @_Z8boolfuncv() -> (!cir.bool {cir.noundef, cir.zeroext})
// CHECK:   %0 = cir.const #true
// CHECK:   cir.return %0 : !cir.bool
// CHECK: }

float floatfunc() { return 42.42f; }
// CHECK: cir.func{{.*}} @_Z9floatfuncv() -> (!cir.float {cir.noundef})
// CHECK:   %0 = cir.const #cir.fp<4.242
// CHECK:   cir.return %0 : !cir.float
// CHECK: }

double doublefunc() { return 42.42; }
// CHECK: cir.func{{.*}} @_Z10doublefuncv() -> (!cir.double {cir.noundef})
// CHECK:   %0 = cir.const #cir.fp<4.242
// CHECK:   cir.return %0 : !cir.double
// CHECK: }
