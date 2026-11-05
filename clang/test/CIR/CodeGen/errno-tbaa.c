// RUN: %clang_cc1 -O1 -disable-llvm-passes -fmath-errno -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -O0 -disable-llvm-passes -fmath-errno -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.O0.ll
// RUN: FileCheck --input-file=%t.O0.ll %s --check-prefix=O0
// RUN: %clang_cc1 -O0 -disable-llvm-passes -fmath-errno -triple x86_64-linux-gnu -fclangir -emit-llvm -fsanitize=thread %s -o %t.tsan.ll
// RUN: FileCheck --input-file=%t.tsan.ll %s --check-prefix=O0
// RUN: %clang_cc1 -O0 -disable-llvm-passes -fmath-errno -triple x86_64-linux-gnu -fclangir -emit-llvm -fsanitize=type %s -o %t.tysan.ll
// RUN: FileCheck --input-file=%t.tysan.ll %s --check-prefix=TYSAN

// Verify CIR matches OGCG for errno TBAA: optimized builds and TypeSanitizer
// O0 keep the canonical "int" TBAA tag alive, while plain O0 and ThreadSanitizer
// O0 do not.

double test(double x) {
  return __builtin_acos(x);
}

// CHECK: call double @acos(double noundef {{.+}}){{.*}}!tbaa ![[ERRNO:[0-9]+]]
// CHECK: !llvm.errno.tbaa = !{![[ERRNO:[0-9]+]]}
// CHECK: ![[ERRNO]] = !{![[INT:[0-9]+]], ![[INT]], i64 0}
// CHECK: ![[INT]] = !{!"int", ![[CHAR:[0-9]+]], i64 0}
// CHECK: ![[CHAR]] = !{!"omnipotent char", ![[ROOT:[0-9]+]], i64 0}
// CHECK: ![[ROOT]] = !{!"Simple C/C++ TBAA"}

// At plain O0 and ThreadSanitizer O0, the int TBAA tag is unavailable.
// O0-NOT: call double @acos(double noundef {{.+}}){{.*}}!tbaa
// O0-NOT: !llvm.errno.tbaa

// TypeSanitizer keeps TBAA enabled even at O0, so both the call metadata and
// the module-level tag remain present.
// TYSAN: call double @acos(double noundef {{.+}}){{.*}}!tbaa ![[ERRNO:[0-9]+]]
// TYSAN: !llvm.errno.tbaa = !{![[ERRNO:[0-9]+]]}
// TYSAN: ![[ERRNO]] = !{![[INT:[0-9]+]], ![[INT]], i64 0}
// TYSAN: ![[INT]] = !{!"int", ![[CHAR:[0-9]+]], i64 0}
// TYSAN: ![[CHAR]] = !{!"omnipotent char", ![[ROOT:[0-9]+]], i64 0}
// TYSAN: ![[ROOT]] = !{!"Simple C/C++ TBAA"}
