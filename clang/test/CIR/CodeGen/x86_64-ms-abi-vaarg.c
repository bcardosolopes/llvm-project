// RUN: %clang_cc1 -triple x86_64-unknown-freebsd10.0 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-freebsd10.0 -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s -check-prefix=OGCG

// Test that CIR handles MS ABI va_arg (__builtin_ms_va_list) on sysv
// targets without crashing. MS va_list uses a simple pointer-bump pattern
// (slot size = 8 bytes, indirect for types > 64 bits or non-power-of-2).

struct big { int a; int b; int c; };

void __attribute__((sysv_abi)) test_ms_vaarg_int(__builtin_ms_va_list ap) {
  int b = __builtin_va_arg(ap, int);
  // LLVM: %[[CUR:.*]] = load ptr, ptr %{{.*}}
  // LLVM: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i64 8
  // LLVM: store ptr %[[NEXT]], ptr %{{.*}}
  // LLVM: load i32, ptr %[[CUR]], align 8

  // OGCG: %[[CUR:.*]] = load ptr, ptr %{{.*}}
  // OGCG: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i64 8
  // OGCG: store ptr %[[NEXT]], ptr %{{.*}}
  // OGCG: load i32, ptr %[[CUR]], align 8
}

void __attribute__((sysv_abi)) test_ms_vaarg_indirect(__builtin_ms_va_list ap) {
  // struct big is 12 bytes (> 8), so it's passed by reference (indirect).
  struct big s = __builtin_va_arg(ap, struct big);
  // LLVM: %[[CUR:.*]] = load ptr, ptr %{{.*}}
  // LLVM: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i64 8
  // LLVM: store ptr %[[NEXT]], ptr %{{.*}}
  // LLVM: load ptr, ptr %[[CUR]]

  // OGCG: %[[CUR:.*]] = load ptr, ptr %{{.*}}
  // OGCG: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i64 8
  // OGCG: store ptr %[[NEXT]], ptr %{{.*}}
  // OGCG: load ptr, ptr %[[CUR]]
}


struct s9 { long long x; char y; };

char __attribute__((sysv_abi)) test_ms_vaarg_tail_padding(__builtin_ms_va_list ap) {
  struct s9 s = __builtin_va_arg(ap, struct s9);
  // LLVM-LABEL: define dso_local signext i8 @test_ms_vaarg_tail_padding(ptr noundef %0)
  // LLVM: %[[DST:.*]] = alloca %struct.s9, i64 1, align 8
  // LLVM: %[[CUR:.*]] = load ptr, ptr %{{.*}}
  // LLVM: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i64 8
  // LLVM: store ptr %[[NEXT]], ptr %{{.*}}
  // LLVM: %[[INDIRECT:.*]] = load ptr, ptr %[[CUR]], align 8
  // LLVM: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %[[DST]], ptr align 8 %[[INDIRECT]], i64 16, i1 false)
  // LLVM: %{{.*}} = getelementptr inbounds nuw %struct.s9, ptr %[[DST]], i32 0, i32 1

  // OGCG-LABEL: define dso_local signext i8 @test_ms_vaarg_tail_padding(ptr noundef %ap)
  // OGCG: %[[CUR:.*]] = load ptr, ptr %{{.*}}
  // OGCG: %[[NEXT:.*]] = getelementptr inbounds i8, ptr %[[CUR]], i64 8
  // OGCG: store ptr %[[NEXT]], ptr %{{.*}}
  // OGCG: %[[INDIRECT:.*]] = load ptr, ptr %[[CUR]], align 8
  // OGCG: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %{{.*}}, ptr align 8 %[[INDIRECT]], i64 16, i1 false)
  return s.y;
}
