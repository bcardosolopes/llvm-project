// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

register unsigned long current_stack_pointer asm("rsp");

struct p4_Thread {
  struct {
    int len;
  } word;
};
// Testing pointer types as well
register struct p4_Thread *p4TH asm("rsp");

// CIR: cir.func {{.*}}@get_stack_pointer_addr()
// CIR:   %[[REG:.*]] = cir.read_register "rsp" : !u64i
// CIR:   cir.return %[[REG]] : !u64i
// LLVM: define{{.*}} i64 @get_stack_pointer_addr()
// LLVM:   call i64 @llvm.read_register.i64(metadata ![[MD:[0-9]+]])
unsigned long get_stack_pointer_addr(void) {
  return current_stack_pointer;
}

// CIR: cir.func {{.*}}@set_stack_pointer_addr
// CIR:   cir.load {{.*}} %{{.*}} : !cir.ptr<!u64i>, !u64i
// CIR:   cir.write_register "rsp" = %{{.*}} : !u64i
// LLVM: define{{.*}} void @set_stack_pointer_addr(i64 noundef %{{.*}})
// LLVM:   call void @llvm.write_register.i64(metadata ![[MD]], i64 %{{.*}})
void set_stack_pointer_addr(unsigned long addr) {
  current_stack_pointer = addr;
}

// CIR: cir.func {{.*}}@fn1
// CIR:   %[[REG:.*]] = cir.read_register "rsp" : !u64i
// LLVM: define {{.*}}@fn1
// LLVM:   %[[REG:[0-9]+]] = call i64 @llvm.read_register.i64(metadata ![[MD]])
// LLVM:   inttoptr i64 %[[REG]] to ptr
int fn1(void) {
  return (*p4TH).word.len;
}

// CIR: cir.func {{.*}}@fn2
// CIR:   cir.write_register "rsp" = %{{.*}} : !u64i
// LLVM: define {{.*}}@fn2
// LLVM:   ptrtoint ptr %{{.*}} to i64
// LLVM:   call void @llvm.write_register.i64(metadata ![[MD]], i64 %{{.*}})
void fn2(struct p4_Thread *val) {
  p4TH = val;
}

// LLVM: !llvm.named.register.rsp = !{![[MD]]}
// LLVM: ![[MD]] = !{!"rsp"}
