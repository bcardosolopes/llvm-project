// RUN: %clang_cc1 -triple arm64-apple-macosx11 -fobjc-arc -fblocks -fclangir -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple arm64-apple-macosx11 -fobjc-arc -fblocks -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=OGCG

// A block that captures BY VALUE a non-trivial C struct (a struct with a
// __strong member under ARC) must emit copy/dispose helpers so the captured
// __strong member is retained on Block_copy and released on Block_release, and
// must destroy the on-stack captured copy at the enclosing scope. Previously
// ClangIR classified such a capture as trivial (None), emitting neither helper
// (BLOCK_HAS_COPY_DISPOSE unset) and never destroying the captured copy — an
// ObjC-ARC leak (__strong) / use-after-free (__weak). Mirrors classic
// CGBlocks.cpp BlockCaptureEntityKind::NonTrivialCStruct.

typedef struct {
  __strong id x;
  int n;
} NTS;

typedef void (^blk_t)(void);
blk_t g;

void take(NTS s) {
  g = ^{ (void)s.n; };
}

// The block descriptor must carry both a copy and a dispose helper, the block
// flags must set BLOCK_HAS_COPY_DISPOSE (0x02000000 → stored flag word
// 0xC2000000 = -1040187392), the copy helper must retain the captured __strong
// member, and the dispose helper must destroy the captured struct.

// LLVM-DAG: ptr @__copy_helper_block_{{[^,]*}}, ptr @__destroy_helper_block_
// LLVM-DAG: store i32 -1040187392
// LLVM-DAG: define {{.*}}@__copy_helper_block_{{.*}} {
// LLVM-DAG: call {{.*}}@llvm.objc.retain
// LLVM-DAG: define {{.*}}@__destroy_helper_block_{{.*}} {
// LLVM-DAG: call void @__destructor_8_s0

// OGCG-DAG: ptr @__copy_helper_block_{{[^,]*}}, ptr @__destroy_helper_block_
// OGCG-DAG: store i32 -1040187392
// OGCG-DAG: define {{.*}}@__copy_helper_block_
// OGCG-DAG: define {{.*}}@__destroy_helper_block_
