// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

int foo(void) { return __extension__ 0b101010; }

//CHECK: cir.func {{.*}} @foo()
//CHECK-NEXT:    [[VAL:%.*]] = cir.const #cir.int<42> : !s32i
//CHECK-NEXT:    cir.return [[VAL]] : !s32i

void bar(void) {
  __extension__ bar;
}

//CHECK:  cir.func {{.*}} @bar()
//CHECK:    {{.*}} = cir.get_global @bar : !cir.ptr<!cir.func<()>>
//CHECK:    cir.return
