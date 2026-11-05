// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s
#define multiline_if_macro(c, t) \
if (c) { \
  return t; \
}

int testMacroLocations(int c) {

  // Expanded macros will use the location of the expansion site.
  multiline_if_macro(c, 3);
  // CHECK: cir.if %{{.+}} {
  // CHECK:   cir.return %{{.+}} : !s32i loc(#loc[[#RETLOC:]])
  // CHECK: } loc(#loc[[#IFLOC:]])

  // Regular if statements should use different locations.
  if (c) {
    return 3;
  }
  //     CHECK: cir.if %{{.+}} {
  //     CHECK:   cir.return %{{.+}} : !s32i loc(#loc[[#RETLOC2:]])
  // CHECK: } loc(#loc[[#IFLOC2:]])
  // CHECK-NOT: #loc[[#RETLOC2]] = loc(fused[#loc[[#IFLOC2]]])

  return 0;
}

void testIfStmtLocations(int f) {
  if (f)
    ;
  else
    ;

  if (f)
    ++f;
  else
    ;

  if (f)
    ;
  else
    --f;

  if (f)
    ++f;
  else
    --f;
}

// CHECK: cir.if %{{.+}} {
// CHECK: } else {
// CHECK: } loc(#loc[[#LOC1:]])

// CHECK: cir.if %{{.+}} {
// CHECK:   %{{.+}} = cir.load
// CHECK:   %{{.+}} = cir.unary(inc
// CHECK:   cir.store
// CHECK: } else {
// CHECK: } loc(#loc[[#LOC2:]])

// CHECK: cir.if %{{.+}} {
// CHECK: } else {
// CHECK:   %{{.+}} = cir.load
// CHECK:   %{{.+}} = cir.unary(dec
// CHECK:   cir.store
// CHECK: } loc(#loc[[#LOC3:]])

// CHECK: cir.if %{{.+}} {
// CHECK:   %{{.+}} = cir.load
// CHECK:   %{{.+}} = cir.unary(inc
// CHECK:   cir.store
// CHECK: } else {
// CHECK:   %{{.+}} = cir.load
// CHECK:   %{{.+}} = cir.unary(dec
// CHECK:   cir.store
// CHECK: } loc(#loc[[#LOC4:]])

// CHECK-DAG: #loc[[#RETLOC]] = loc(fused<{llvm.di_scope_chain = [11 : i32, 3 : i32], llvm.di_scope_column = 3 : i32, llvm.di_scope_line = 11 : i32}>[#loc[[#IFLOC]]])
// CHECK-DAG: #loc[[#RETLOC2]] = loc(fused<{llvm.di_scope_chain = [17 : i32, 10 : i32], llvm.di_scope_column = 10 : i32, llvm.di_scope_line = 17 : i32}>[#loc{{[0-9]+}}])

// CHECK-DAG: #loc[[#LOC11:]] = loc({{.+}}:30:5)
// CHECK-DAG: #loc[[#LOC12:]] = loc({{.+}}:32:5)

// CHECK-DAG: #loc[[#LOC21:]] = loc({{.+}}:35:5)
// CHECK-DAG: #loc[[#LOC22:]] = loc({{.+}}:35:7)
// CHECK-DAG: #loc[[#LOC23:]] = loc({{.+}}:37:5)

// CHECK-DAG: #loc[[#LOC31:]] = loc({{.+}}:40:5)
// CHECK-DAG: #loc[[#LOC32:]] = loc({{.+}}:42:5)
// CHECK-DAG: #loc[[#LOC33:]] = loc({{.+}}:42:7)

// CHECK-DAG: #loc[[#LOC41:]] = loc({{.+}}:45:5)
// CHECK-DAG: #loc[[#LOC42:]] = loc({{.+}}:45:7)
// CHECK-DAG: #loc[[#LOC43:]] = loc({{.+}}:47:5)
// CHECK-DAG: #loc[[#LOC44:]] = loc({{.+}}:47:7)

// CHECK-DAG: #loc[[#LOC1]] = loc(fused[#loc[[#LOC11]], #loc[[#LOC12]]])
// CHECK-DAG: #loc[[#LOC2]] = loc(fused[#loc[[#LOC21]], #loc[[#LOC22]], #loc[[#LOC23]]])
// CHECK-DAG: #loc[[#LOC3]] = loc(fused[#loc[[#LOC31]], #loc[[#LOC32]], #loc[[#LOC33]]])
// CHECK-DAG: #loc[[#LOC4]] = loc(fused[#loc[[#LOC41]], #loc[[#LOC42]], #loc[[#LOC43]], #loc[[#LOC44]]])
