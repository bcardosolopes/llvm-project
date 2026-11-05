// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm %s \
// RUN:   -o %t.cir.ll -disable-llvm-passes -fsanitize=memtag-globals
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm %s \
// RUN:   -o %t.ogcg.ll -disable-llvm-passes -fsanitize=memtag-globals
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that -fsanitize=memtag-globals emits sanitize_memtag on globals,
// matching OGCG behavior.

int global_var = 42;
static int static_var = 100;

int test(void) {
  return global_var + static_var;
}

// LLVM: @global_var = global i32 42, sanitize_memtag, align 4
// LLVM: @static_var = internal global i32 100, sanitize_memtag, align 4

// OGCG: @global_var = global i32 42, sanitize_memtag, align 4
// OGCG: @static_var = internal global i32 100, sanitize_memtag, align 4
