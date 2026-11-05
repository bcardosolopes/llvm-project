// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-flat %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

typedef signed char (*signed_char_fn_t)(signed char);
typedef unsigned char (*unsigned_char_fn_t)(unsigned char);

signed char call_signed_indirect(signed_char_fn_t fn, signed char v) {
  return fn(v);
}

unsigned char call_unsigned_indirect(unsigned_char_fn_t fn, unsigned char v) {
  return fn(v);
}

// CIR: cir.call %{{.*}}(%{{.*}}) {cir.call_arg_attrs = [{cir.noundef, cir.signext}], cir.call_res_attrs = {cir.signext}}
// LLVM: call signext i8 %{{.*}}(i8 noundef signext %{{.*}})
// CIR: cir.call %{{.*}}(%{{.*}}) {cir.call_arg_attrs = [{cir.noundef, cir.zeroext}], cir.call_res_attrs = {cir.zeroext}}
// LLVM: call zeroext i8 %{{.*}}(i8 noundef zeroext %{{.*}})
