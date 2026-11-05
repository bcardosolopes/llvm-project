// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -Wno-unused-value -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

struct Bar {
  float f1;
  float f2;
  unsigned u;
};

struct Bar varargs_aggregate(int count, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, count);
  struct Bar res = __builtin_va_arg(args, struct Bar);
  __builtin_va_end(args);
  return res;
}

// CIR-LABEL: "cir.func"() <{{.*}}sym_name = "varargs_aggregate"
// CIR: "cir.va_start"
// CIR: %{{.*}} = "cir.scope"()
// CIR: %{{.*}} = "cir.get_member"({{.*}}) <{index_attr = 0 : index, name = "gp_offset"}>
// CIR: %{{.*}} = "cir.cmp"({{.*}}) <{{.*}}kind = 1 : i32}>
// CIR: %{{.*}} = "cir.get_member"({{.*}}) <{index_attr = 1 : index, name = "fp_offset"}>
// CIR: %{{.*}} = "cir.cmp"({{.*}}) <{{.*}}kind = 1 : i32}>
// CIR: %{{.*}} = "cir.binop"({{.*}}) <{{.*}}kind = 5 : i32}>
// CIR: "cir.brcond"
// CIR: ^bb1:
// CIR: %{{.*}} = "cir.get_member"({{.*}}) <{index_attr = 3 : index, name = "reg_save_area"}>
// CIR: "cir.br"({{.*}})[^bb3]
// CIR: ^bb2:
// CIR: %{{.*}} = "cir.get_member"({{.*}}) <{index_attr = 2 : index, name = "overflow_arg_area"}>
// CIR: "cir.br"({{.*}})[^bb3]
// CIR: ^bb3(%{{.*}}: !cir.ptr<!rec_Bar>
// CIR: "cir.libc.memcpy"({{.*}}) {cir.dst_align = 4 : i64, cir.src_align = 4 : i64}
// CIR: "cir.yield"({{.*}})
// CIR: "cir.va_end"({{.*}})
// CIR: "cir.libc.memcpy"({{.*}}) {cir.dst_align = 8 : i64, cir.src_align = 4 : i64}

// LLVM-LABEL: define dso_local { <2 x float>, i32 } @varargs_aggregate(
// LLVM:   call void @llvm.va_start.p0(ptr %{{.*}})
// LLVM:   icmp ule i32 %{{.+}}, 40
// LLVM:   icmp ule i32 %{{.+}}, 160
// LLVM:   and i1
// LLVM:   br i1
// LLVM:   phi ptr
// LLVM:   call void @llvm.memcpy.p0.p0.i64(ptr{{.*}} %{{.*}}, ptr{{.*}} %{{.*}}, i64 12, i1 false)

// OGCG-LABEL: define dso_local { <2 x float>, i32 } @varargs_aggregate
// OGCG:   call void @llvm.va_start.p0(ptr %{{.*}})
// OGCG:   %[[VAARG_ADDR:.+]] = phi ptr [ %{{.*}}, %vaarg.in_reg ], [ %{{.*}}, %vaarg.in_mem ]
// OGCG:   call void @llvm.memcpy.p0.p0.i64(ptr align 4 %{{.*}}, ptr align 4 %[[VAARG_ADDR]], i64 12, i1 false)
