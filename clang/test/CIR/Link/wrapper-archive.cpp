// How cir-link-wrapper treats static archives, checked by giving it a
// "linker" that just prints what it was handed.
//
// With --cir-merge-archives the wrapper looks inside directly-named `*.a
// files for members carrying CIR, extracts those members, and merges them
// with the loose objects. The archive itself stays on the command line:
// members without CIR keep lazy archive semantics, and the linker never
// pulls a merged member because the merged bitcode already defines its
// symbols. Without the flag, and inside --whole-archive or a lazy group,
// archives pass through untouched.
//
// The negatives are --implicit-check-not, not CHECK-NOT: a CHECK-NOT is only
// matched against the region after the preceding CHECK, and since /bin/echo
// prints one line these would all search an empty region and never fail.
//
// REQUIRES: system-linux

// RUN: rm -rf %t && mkdir -p %t
// RUN: %clang -fclangir -fclangir-fat-objects --target=%itanium_abi_triple \
// RUN:     -c %s -o %t/fat.o
// RUN: %clang --target=%itanium_abi_triple -c %s -o %t/plain.o
// RUN: llvm-ar rcs %t/libfat.a %t/fat.o %t/plain.o
// RUN: llvm-ar rcs %t/libplain.a %t/plain.o

//--- Without the flag an archive passes through untouched, even when its
//    members carry CIR.
// RUN: cir-link-wrapper --linker-path=/bin/echo \
// RUN:     %t/plain.o %t/libfat.a -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=FLAG-OFF --implicit-check-not=merged.bc
// FLAG-OFF: {{.*}}plain.o {{.*}}libfat.a -o {{.*}}out.so -shared

//--- With the flag the CIR member is extracted and merged, and the archive
//    stays on the line for its remaining members. /bin/echo stands in for
//    cir-link so the merge set is printed rather than run: exactly one
//    member comes out, the fat one, not the plain one.
// RUN: cir-link-wrapper --linker-path=/bin/echo --cir-link-path=/bin/echo \
// RUN:     --cir-merge-archives --cir-triple=%itanium_abi_triple \
// RUN:     %t/plain.o %t/libfat.a -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=MERGE --implicit-check-not=member-1.o
// MERGE: member-0.o --emit-llvm
// MERGE: {{.*}}merged.bc {{.*}}libfat.a -o {{.*}}out.so -shared

//--- An archive with no CIR anywhere is untouched even with the flag.
// RUN: cir-link-wrapper --linker-path=/bin/echo --cir-link-path=/bin/echo \
// RUN:     --cir-merge-archives --cir-triple=%itanium_abi_triple \
// RUN:     %t/plain.o %t/libplain.a -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=PLAIN --implicit-check-not=merged.bc
// PLAIN: {{.*}}plain.o {{.*}}libplain.a -o {{.*}}out.so -shared

//--- Under --whole-archive every member links unconditionally, so absorbing
//    members would duplicate every definition. Pass through instead.
// RUN: cir-link-wrapper --linker-path=/bin/echo --cir-link-path=/bin/echo \
// RUN:     --cir-merge-archives --cir-triple=%itanium_abi_triple \
// RUN:     --whole-archive %t/libfat.a --no-whole-archive -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=WHOLE --implicit-check-not=merged.bc
// WHOLE: --whole-archive {{.*}}libfat.a --no-whole-archive

//--- Inside a lazy group the explicit only-if-needed request wins over
//    coverage, the same rule loose objects follow.
// RUN: cir-link-wrapper --linker-path=/bin/echo --cir-link-path=/bin/echo \
// RUN:     --cir-merge-archives --cir-triple=%itanium_abi_triple \
// RUN:     --start-lib %t/libfat.a --end-lib -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LAZY --implicit-check-not=merged.bc
// LAZY: --start-lib {{.*}}libfat.a --end-lib

//--- A `.a` that is not an archive -- a linker script is legal under any
//    name -- passes through rather than crashing the probe.
// RUN: echo 'GROUP ( libfoo.a )' > %t/script.a
// RUN: cir-link-wrapper --linker-path=/bin/echo --cir-link-path=/bin/echo \
// RUN:     --cir-merge-archives --cir-triple=%itanium_abi_triple \
// RUN:     %t/plain.o %t/script.a -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SCRIPT --implicit-check-not=merged.bc
// SCRIPT: {{.*}}plain.o {{.*}}script.a -o {{.*}}out.so -shared

//--- An archive named twice contributes its members once: the second
//    occurrence adds no new extraction.
// RUN: cir-link-wrapper --linker-path=/bin/echo --cir-link-path=/bin/echo \
// RUN:     --cir-merge-archives --cir-triple=%itanium_abi_triple \
// RUN:     %t/plain.o %t/libfat.a %t/libfat.a -o %t/out.so -shared 2>&1 \
// RUN:   | FileCheck %s --check-prefix=REPEAT --implicit-check-not=member-1.o
// REPEAT: member-0.o --emit-llvm
// REPEAT: {{.*}}libfat.a {{.*}}libfat.a

struct S {
  int x;
};
int g(S *s) { return s->x; }
