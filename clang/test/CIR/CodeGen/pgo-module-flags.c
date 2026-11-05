// RUN: llvm-profdata merge -o %t.profdata %S/../../CodeGen/Inputs/pgotestclang.profraw
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -Wno-profile-instr-unprofiled \
// RUN:   -fprofile-instrument-use=clang -fprofile-instrument-use-path=%t.profdata \
// RUN:   -emit-llvm %s -o %t.use.ll
// RUN: FileCheck --check-prefix=USE --input-file=%t.use.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fprofile-instrument=clang -emit-llvm %s -o %t.instr.ll
// RUN: FileCheck --check-prefix=INSTR --input-file=%t.instr.ll %s

int f(int x) { return x ? 1 : 2; }

// USE-DAG: ![[WCHAR:[0-9]+]] = !{i32 1, !"wchar_size", i32 4}
// USE-DAG: ![[PROFILE:[0-9]+]] = !{i32 1, !"ProfileSummary", ![[SUMMARY:[0-9]+]]}
// USE: ![[SUMMARY]] = !{![[FMT:[0-9]+]], {{.*}}}
// USE: ![[FMT]] = !{!"ProfileFormat", !"InstrProf"}

// INSTR-DAG: ![[WCHAR:[0-9]+]] = !{i32 1, !"wchar_size", i32 4}
// INSTR-DAG: ![[VALUEPROF:[0-9]+]] = !{i32 2, !"EnableValueProfiling", i32 0}
