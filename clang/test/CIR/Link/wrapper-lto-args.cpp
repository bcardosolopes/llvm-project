// The build tells the wrapper what kind of LTO link this is, and the wrapper
// has to pass that on to whichever tool writes the merged module -- cir-link by
// default, cir-translate under --cir-split-lowering. The two paths used to
// build their command lines separately, and the split path lost the LTO
// arguments: it wrote summary-less bitcode, which lld rejects with
// "inconsistent LTO Unit splitting", in exactly the configuration meant to
// rescue a link from running out of memory.
//
// /bin/echo stands in for every tool, so each invocation is printed rather than
// run and the arguments can be checked directly.
//
// RUN: rm -rf %t && mkdir -p %t
// RUN: %clang -fclangir -fclangir-fat-objects --target=%itanium_abi_triple \
// RUN:     -c %s -o %t/fat.o

//--- In-process: cir-link merges, lowers and writes.
// RUN: cir-link-wrapper --linker-path=/bin/echo --cir-link-path=/bin/echo \
// RUN:     --cir-triple=%itanium_abi_triple --cir-lto=thin --cir-split-lto-unit \
// RUN:     %t/fat.o -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DIRECT
// DIRECT: --shared-library-symbols={{[^ ]*}}shared-library-symbols.txt {{.*}}fat.o
// DIRECT-SAME: --emit-llvm --lto=thin --split-lto-unit -o

//--- Split: cir-translate writes, so cir-translate is the one that needs them,
//    and cir-link's merge step must not be given them.
// RUN: cir-link-wrapper --linker-path=/bin/echo --cir-link-path=/bin/echo \
// RUN:     --cir-translate-path=/bin/echo --cir-split-lowering \
// RUN:     --cir-triple=%itanium_abi_triple --cir-lto=full \
// RUN:     %t/fat.o -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SPLIT
// SPLIT: --shared-library-symbols={{[^ ]*}} {{.*}}fat.o -o {{[^ ]*}}merged.cir{{$}}
// SPLIT-NEXT: -cir-to-llvmir --emit-llvm-bitcode -o {{[^ ]*}}merged.bc --lto=full

//--- The inliner options travel with them, to whichever tool lowers.
// RUN: cir-link-wrapper --linker-path=/bin/echo --cir-link-path=/bin/echo \
// RUN:     --cir-triple=%itanium_abi_triple --cir-inline \
// RUN:     --cir-inline-threshold=8 %t/fat.o -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=INLINE
// INLINE: --emit-llvm --inline --cir-inline-threshold=8 -o
// RUN: cir-link-wrapper --linker-path=/bin/echo --cir-link-path=/bin/echo \
// RUN:     --cir-translate-path=/bin/echo --cir-split-lowering \
// RUN:     --cir-triple=%itanium_abi_triple --cir-inline \
// RUN:     --cir-inline-threshold=8 %t/fat.o -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SPLIT-INLINE
// SPLIT-INLINE: fat.o -o {{[^ ]*}}merged.cir{{$}}
// SPLIT-INLINE-NEXT: -cir-to-llvmir --emit-llvm-bitcode -o {{[^ ]*}}merged.bc --inline --cir-inline-threshold=8

//--- --cir-partitions=<N>: cir-link writes N pieces, piece k as merged.bc.k,
//    and every piece takes the merged objects' place on the link line.
// RUN: cir-link-wrapper --linker-path=/bin/echo --cir-link-path=/bin/echo \
// RUN:     --cir-triple=%itanium_abi_triple --cir-partitions=3 \
// RUN:     %t/fat.o -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PARTS
// PARTS: --emit-llvm --partitions=3 -o {{[^ ]*}}merged.bc{{ }}
// PARTS-NEXT: {{^[^ ]*}}merged.bc {{[^ ]*}}merged.bc.1 {{[^ ]*}}merged.bc.2 -o {{[^ ]*}}out.so -shared{{$}}
// RUN: not cir-link-wrapper --linker-path=/bin/echo --cir-split-lowering \
// RUN:     --cir-partitions=2 %t/fat.o -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PARTS-SPLIT
// PARTS-SPLIT: --cir-partitions is not supported with --cir-split-lowering
// RUN: not cir-link-wrapper --linker-path=/bin/echo --cir-partitions=0 \
// RUN:     %t/fat.o -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PARTS-BAD
// PARTS-BAD: --cir-partitions='0': expected a positive integer

//--- Told nothing, it passes nothing: a plain bitcode write, not a guess.
// RUN: cir-link-wrapper --linker-path=/bin/echo --cir-link-path=/bin/echo \
// RUN:     --cir-triple=%itanium_abi_triple %t/fat.o -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NONE --implicit-check-not=--lto \
// RUN:       --implicit-check-not=--split-lto-unit
// NONE: --emit-llvm -o

//--- A kind it does not know is an error here, naming the option the build
//    passed, rather than something cir-link rejects later in its own terms.
// RUN: not cir-link-wrapper --linker-path=/bin/echo --cir-lto=fat \
// RUN:     %t/fat.o -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD
// BAD: --cir-lto='fat': expected full or thin

struct S {
  int x;
};
int g(S *s) { return s->x; }
