// -fstd-lib-stats=embed parks the report in the .stdlib_stats section of the
// object instead of writing it to stderr, so it can be harvested from the
// per-TU .o files of a whole-app build. Both code generators must do it.

// Classical (non-CIR) codegen.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fstd-lib-stats=embed \
// RUN:   -emit-obj %s -o %t.ogcg.o 2>&1 | FileCheck %s -check-prefix=SILENT \
// RUN:   --allow-empty
// RUN: llvm-objcopy --dump-section .stdlib_stats=%t.ogcg.json %t.ogcg.o \
// RUN:   /dev/null
// RUN: FileCheck %s -check-prefix=REPORT --input-file=%t.ogcg.json

// CIR codegen.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fstd-lib-stats=embed -emit-obj %s -o %t.cir.o 2>&1 \
// RUN:   | FileCheck %s -check-prefix=SILENT --allow-empty
// RUN: llvm-objcopy --dump-section .stdlib_stats=%t.cir.json %t.cir.o /dev/null
// RUN: FileCheck %s -check-prefix=REPORT --input-file=%t.cir.json

// The two code generators produce the same report: it comes from the AST.
// RUN: diff %t.ogcg.json %t.cir.json

// json mode leaves the object alone.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fstd-lib-stats=json \
// RUN:   -emit-obj %s -o %t.json.o 2>/dev/null
// RUN: not llvm-objcopy --dump-section .stdlib_stats=%t.absent %t.json.o \
// RUN:   /dev/null

namespace std {
struct string {
  unsigned size() const;
};
} // namespace std

int use(const std::string &s) { return s.size(); }

// SILENT-NOT: stdlib_stats

// REPORT: {"version":1,"kind":"stdlib_stats","tu":"{{.*}}stdlib-stats-embed.cpp"
// REPORT-SAME: {"record":"string","function":"size()","count":1,
// REPORT-SAME: "callers":[{"name":"use","mangled":"_Z3useRKSt6string","count":1}]

// Embed mode drops the unbounded per-call-site location list: at whole-app
// scale it is the difference between megabytes and gigabytes.
// REPORT-NOT: "locations"
