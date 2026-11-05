// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=constructor -dwarf-version=5 %s -o - \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -debug-info-kind=constructor -dwarf-version=5 %s -o - \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm \
// RUN:   -debug-info-kind=constructor -dwarf-version=5 %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=NODEBUG

// __builtin_verbose_trap emits a plain trap plus, when debug info is on, an
// artificial __clang_trap_msg$<category>$<message> DISubprogram whose frame the
// trap's !dbg is inlined at, so the debugger/crash-reporter shows the reason.
// This mirrors classic CGDebugInfo::CreateTrapFailureMessageFor. Without -g it
// is a plain trap with no annotation.

void chk(int x) {
  if (x < 0)
    __builtin_verbose_trap("Bounds", "index out of range");
}

// LLVM-LABEL: @chk
// LLVM: call void @llvm.trap(), !dbg ![[TRAPLOC:[0-9]+]]
// LLVM: ![[TRAPLOC]] = !DILocation(line: 0, scope: ![[TRAPSP:[0-9]+]], inlinedAt: {{![0-9]+}})
// LLVM: ![[TRAPSP]] = distinct !DISubprogram(name: "__clang_trap_msg$Bounds$index out of range", {{.*}}flags: DIFlagArtificial, spFlags: DISPFlagDefinition

// OGCG-LABEL: @chk
// OGCG: call void @llvm.trap(), !dbg ![[OTRAPLOC:[0-9]+]]
// OGCG: ![[OTRAPLOC]] = !DILocation(line: 0, scope: ![[OTRAPSP:[0-9]+]], inlinedAt: {{![0-9]+}})
// OGCG: ![[OTRAPSP]] = distinct !DISubprogram(name: "__clang_trap_msg$Bounds$index out of range", {{.*}}flags: DIFlagArtificial, spFlags: DISPFlagDefinition

// NODEBUG-LABEL: @chk
// NODEBUG: call void @llvm.trap()
// NODEBUG-NOT: __clang_trap_msg
