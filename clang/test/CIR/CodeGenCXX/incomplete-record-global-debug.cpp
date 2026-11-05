// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir \
// RUN:   -emit-llvm -debug-info-kind=constructor %s -o /dev/null
//
// Regression test for a crash in CIR global emission when debug-info metadata
// attempted to query ASTRecordLayout for a forward-declared record.

struct ForwardDecl;

extern ForwardDecl *GlobalPtr;

bool useGlobalPtr() { return GlobalPtr != nullptr; }
