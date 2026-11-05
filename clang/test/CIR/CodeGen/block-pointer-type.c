// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fblocks -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fblocks -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// BlockPointer type support — blocks are opaque pointers (void*).

typedef void (^block_t)(void);
void use_block(block_t b);

void test_block_ptr(void) {
  block_t b = (block_t)0;
  use_block(b);
}

// CIR: %{{.*}} = cir.alloca !cir.ptr<!void>, !cir.ptr<!cir.ptr<!void>>
// CIR: cir.call @use_block(%{{.*}}) : (!cir.ptr<!void>) -> ()

// LLVM: call void @use_block(ptr
