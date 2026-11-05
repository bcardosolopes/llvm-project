// A cross-library optimizer's codegen link leaves out the inputs the single-library link listed
// in its constituents file, and links the library's code from its
// object instead. That link only ever sees the wrapper's merged pieces, so the
// wrapper puts the objects merged into them back in the list: otherwise the
// codegen link linked them again next to the optimizer's object, every symbol they
// define duplicated. An object the wrapper passed through keeps its line.
//
// RUN: rm -rf %t && mkdir -p %t
// RUN: %clang -fclangir -fclangir-fat-objects --target=%itanium_abi_triple \
// RUN:     -c %s -o %t/fat.o
// RUN: %clang -fclangir -fclangir-fat-objects --target=%itanium_abi_triple \
// RUN:     -DSECOND -c %s -o %t/fat2.o
// RUN: %clang --target=%itanium_abi_triple -DPLAIN -c %s -o %t/plain.o

// RUN: cir-link-wrapper --linker-path=%S/Inputs/constituents-ld.sh \
// RUN:     --cir-link-path=/bin/echo --cir-triple=%itanium_abi_triple \
// RUN:     %t/plain.o %t/fat.o %t/fat2.o -o %t/out.so -shared \
// RUN:     --constituents-file=%t/one.txt
// RUN: FileCheck --input-file=%t/one.txt %s --check-prefix=ONE \
// RUN:     --implicit-check-not=merged
// ONE:      {{^}}{{.*}}plain.o{{$}}
// ONE-NEXT: {{^}}{{.*}}fat.o{{$}}
// ONE-NEXT: {{^}}{{.*}}fat2.o{{$}}

// With the merged module in pieces, all of them give way to the objects once.
// RUN: cir-link-wrapper --linker-path=%S/Inputs/constituents-ld.sh \
// RUN:     --cir-link-path=/bin/echo --cir-triple=%itanium_abi_triple \
// RUN:     --cir-partitions=3 %t/fat.o %t/plain.o %t/fat2.o -o %t/out.so \
// RUN:     -shared --constituents-file=%t/parts.txt
// RUN: FileCheck --input-file=%t/parts.txt %s --check-prefix=PARTS \
// RUN:     --implicit-check-not=merged
// PARTS:      {{^}}{{.*}}fat.o{{$}}
// PARTS-NEXT: {{^}}{{.*}}fat2.o{{$}}
// PARTS-NEXT: {{^}}{{.*}}plain.o{{$}}

// The option as two arguments names the same file.
// RUN: cir-link-wrapper --linker-path=%S/Inputs/constituents-ld.sh \
// RUN:     --cir-link-path=/bin/echo --cir-triple=%itanium_abi_triple \
// RUN:     %t/fat.o -o %t/out.so -shared --constituents-file %t/split.txt
// RUN: FileCheck --input-file=%t/split.txt %s --check-prefix=SPLIT \
// RUN:     --implicit-check-not=merged
// SPLIT: {{^}}{{.*}}fat.o{{$}}

// A member merged out of an archive is not listed, nor is the archive: it
// stays on the codegen link for the members without CIR.
// RUN: llvm-ar rcs %t/libfat.a %t/fat2.o
// RUN: cir-link-wrapper --linker-path=%S/Inputs/constituents-ld.sh \
// RUN:     --cir-link-path=/bin/echo --cir-triple=%itanium_abi_triple \
// RUN:     --cir-merge-archives %t/plain.o %t/libfat.a -o %t/out.so -shared \
// RUN:     --constituents-file=%t/archive.txt
// RUN: FileCheck --input-file=%t/archive.txt %s --check-prefix=ARCHIVE \
// RUN:     --implicit-check-not=merged --implicit-check-not=member \
// RUN:     --implicit-check-not=fat
// ARCHIVE: {{^}}{{.*}}plain.o{{$}}

// A linker that lists none of the pieces still gets the objects, with a
// warning, rather than a list that brings the duplicates back.
// RUN: cir-link-wrapper --linker-path=%S/Inputs/constituents-ld-objects.sh \
// RUN:     --cir-link-path=/bin/echo --cir-triple=%itanium_abi_triple \
// RUN:     %t/plain.o %t/fat.o -o %t/out.so -shared \
// RUN:     --constituents-file=%t/unlisted.txt 2>&1 \
// RUN:   | FileCheck %s --check-prefix=UNLISTED-WARN
// UNLISTED-WARN: warning: '{{.*}}unlisted.txt' lists none of the merged pieces; appending the 1 objects merged into them
// RUN: FileCheck --input-file=%t/unlisted.txt %s --check-prefix=UNLISTED \
// RUN:     --implicit-check-not=merged
// UNLISTED:      {{^}}{{.*}}plain.o{{$}}
// UNLISTED-NEXT: {{^}}{{.*}}fat.o{{$}}

// A link that wrote no file succeeds, and there is nothing to rewrite.
// RUN: cir-link-wrapper --linker-path=/bin/true --cir-link-path=/bin/echo \
// RUN:     --cir-triple=%itanium_abi_triple %t/fat.o -o %t/out.so -shared \
// RUN:     --constituents-file=%t/none.txt 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NONE
// RUN: not ls %t/none.txt
// NONE: warning: the linker wrote no '{{.*}}none.txt'; nothing to rewrite

#if defined(PLAIN)
int plain() { return 1; }
#elif defined(SECOND)
int second() { return 2; }
#else
int first() { return 3; }
#endif
