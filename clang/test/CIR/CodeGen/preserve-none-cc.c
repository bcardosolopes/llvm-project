// RUN: %clang_cc1 -fclangir -emit-llvm -triple x86_64-unknown-linux-gnu %s -o /dev/null

// preserve_none calling convention — fixed by adding CC_PreserveNone to CIR CallingConv enum

__attribute__((preserve_none)) void bar(void) {}
