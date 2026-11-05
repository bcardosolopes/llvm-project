// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll --check-prefix=OGCG %s

// Test that noreturn attribute is properly emitted for function declarations
// and propagated to LLVM IR, and that unreachable is emitted after noreturn
// calls (both direct and indirect).

// GNU __attribute__((noreturn))
__attribute__((noreturn)) void die(void);
// C11 _Noreturn
_Noreturn void fatal(void);

void test_noreturn(int x) {
  if (x)
    die();
  else
    fatal();
}

// CIR-DAG: #[[NR_CALL_ATTR:fn_attr[0-9]*]] = #cir<extra({noreturn = #cir.noreturn})>
// CIR-DAG: #[[NR_DECL_ATTR:fn_attr[0-9]*]] = #cir<extra({{{.*}}noreturn = #cir.noreturn{{.*}}target-features{{.*}}})>
// CIR: cir.call @die() : () -> () extra(#[[NR_CALL_ATTR]])
// CIR-NEXT: cir.unreachable
// CIR: cir.call @fatal() : () -> () extra(#[[NR_CALL_ATTR]])
// CIR-NEXT: cir.unreachable
// CIR: cir.func private @fatal() extra(#[[NR_DECL_ATTR]])
// CIR: cir.func private @die() extra(#[[NR_DECL_ATTR]])

// LLVM: call void @die()
// LLVM-NEXT: unreachable
// LLVM: call void @fatal()
// LLVM-NEXT: unreachable

// Test unreachable after indirect noreturn call via function pointer.
typedef void (*noreturn_fp)(void) __attribute__((noreturn));
noreturn_fp p[4];

__attribute__((noreturn)) void f_indirect(void) {
  p[0]();
}

// CIR-LABEL: cir.func {{.*}} @f_indirect()
// CIR:         cir.call %{{.*}}() {{.*}} extra(#{{.*}})
// CIR-NEXT:    cir.unreachable

// LLVM-LABEL: define {{.*}} void @f_indirect()
// LLVM:         call void %{{.*}}()
// LLVM-NEXT:    unreachable

// OGCG-LABEL: define {{.*}} void @f_indirect()
// OGCG:         call void %{{.*}}()
// OGCG-NEXT:    unreachable
