// Fallback when the merged link fails: the wrapper retries the exact
// original command line unmerged. Eager member extraction breaks archive
// lazy semantics -- a member nothing needs is now present anyway, uses
// and all, so a link that resolved lazily can die merged on an undefined
// symbol. The retry still produces a working binary -- the plain-LTO
// link -- with a loud warning instead of a failed build.
//
// The fake linker fails any command carrying the merged bitcode and
// passes anything else: the merged link fails, the original does not.
//
// Loose-only merges never trigger the fallback: without absorbed archive
// members there is no laziness change, so a failed link there is a real
// problem and stays loud.
//
// REQUIRES: system-linux

// RUN: rm -rf %t && mkdir -p %t
// RUN: %clang -fclangir -fclangir-fat-objects --target=%itanium_abi_triple \
// RUN:     -c %s -o %t/fat.o
// RUN: llvm-ar rcs %t/libfat.a %t/fat.o
// RUN: printf '#!/bin/sh\necho "LINK: $@"\nfor a in "$@"; do case "$a" in *merged.bc*) exit 1;; esac; done\nexit 0\n' > %t/fake-ld.sh && chmod +x %t/fake-ld.sh

//--- Merged link fails, retry with the original inputs succeeds.
// RUN: cir-link-wrapper --linker-path=%t/fake-ld.sh --cir-link-path=/bin/echo \
// RUN:     --cir-merge-archives --cir-triple=%itanium_abi_triple \
// RUN:     %t/fat.o %t/libfat.a -o %t/out.so -shared > %t/log.txt 2>&1
// RUN: FileCheck %s < %t/log.txt
// CHECK: LINK: {{.*}}merged.bc
// CHECK: cir-link-wrapper: warning: {{.*}}retrying with the original unmerged inputs
// CHECK: LINK: {{[^ ]*}}fat.o {{[^ ]*}}libfat.a -o {{[^ ]*}}out.so -shared{{$}}

//--- Without absorbed archives a failed link stays failed: no retry.
// RUN: printf '#!/bin/sh\nexit 1\n' > %t/always-fail.sh && chmod +x %t/always-fail.sh
// RUN: not cir-link-wrapper --linker-path=%t/always-fail.sh --cir-link-path=/bin/echo \
// RUN:     --cir-triple=%itanium_abi_triple \
// RUN:     %t/fat.o -o %t/out2.so -shared > %t/log2.txt 2>&1
// RUN: FileCheck %s --check-prefix=LOUD < %t/log2.txt
// LOUD-NOT: retrying with the original unmerged inputs

struct S {
  int x;
};
int g(S *s) { return s->x; }
