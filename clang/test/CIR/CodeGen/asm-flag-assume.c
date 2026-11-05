// RUN: %clang_cc1 -fclangir -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=CIR %s -input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s -input-file=%t.ogcg.ll

// Inline asm flag constraint outputs should get llvm.assume for
// boolean range information.

int test_flag_output(int a, int b) {
  int result;
  unsigned char flag;
  asm("cmpxchgl %[new], %[mem]"
      : "=a"(result), "=@cca"(flag)
      : [new]"r"(b), [mem]"m"(a), "a"(0)
      : "memory");
  return flag;
}

int test_flag_output_int(int a, int b) {
  int flag;
  asm("cmpxchgl %[new], %[mem]"
      : "=@cca"(flag)
      : [new]"r"(b), [mem]"m"(a), "a"(0)
      : "memory");
  return flag;
}

// CIR: icmp ult i8 %{{.*}}, 2
// CIR: call void @llvm.assume(i1 %{{.*}})
// CIR: icmp ult i32 %{{.*}}, 2
// CIR: call void @llvm.assume(i1 %{{.*}})
// OGCG: icmp ult i8 %{{.*}}, 2
// OGCG: call void @llvm.assume(i1 %{{.*}})
// OGCG: icmp ult i32 %{{.*}}, 2
// OGCG: call void @llvm.assume(i1 %{{.*}})
