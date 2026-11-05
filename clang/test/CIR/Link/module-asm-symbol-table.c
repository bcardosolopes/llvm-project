// A module with file-scope asm written as LTO bitcode: the writer's symbol
// table parses the asm, which needs the target's asm parser registered in
// cir-link, or it asserts (libearlystartup.so). The symbol the asm defines is
// in the bitcode's symbol table.
//
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -c %s -o %t.o
// RUN: cir-link %t.o --emit-llvm --lto=full --split-lto-unit -o %t.bc
// RUN: llvm-nm %t.bc | FileCheck %s
//
// CHECK-DAG: T from_asm
// CHECK-DAG: T use

__asm__(".globl from_asm\n.type from_asm,%function\nfrom_asm:\n  ret\n");
int use(void) { return 1; }
