// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// CIR omits `noundef` on return type for indirect calls

struct Base {
  virtual int get_val() { return 42; }
  virtual ~Base() {}
};

// CIR-LABEL: @_Z10test_vcallP4Base
// CIR: call noundef i32
// OGCG-LABEL: @_Z10test_vcallP4Base
// OGCG: call noundef i32
int test_vcall(Base *b) { return b->get_val(); }

typedef int (*fptr)(int);

// CIR-LABEL: @_Z9test_fptr
// CIR: call noundef i32 %
// OGCG-LABEL: @_Z9test_fptr
// OGCG: call noundef i32 %
int test_fptr(fptr f, int x) { return f(x); }

// Direct call should already have noundef (works correctly)
int direct_fn(int x) { return x + 1; }

// CIR-LABEL: @_Z10test_dcall
// CIR: call noundef i32 @_Z9direct_fn
// OGCG-LABEL: @_Z10test_dcall
// OGCG: call noundef i32 @_Z9direct_fn
int test_dcall(int x) { return direct_fn(x); }
