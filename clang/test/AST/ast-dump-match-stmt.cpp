// RUN: %clang_cc1 -fsyntax-only -fpattern-matching -Wno-unused-value -ast-dump %s | FileCheck %s

void test_match0(int a, int b) {
  3 match {
    _ => 0;
  };
  // CHECKYOLO: `-CompoundStmt 0x11f85efb8 <col:66, line:7:1>
  // CHECKYOLO: `-MatchExpr 0x11f85ef30 <line:4:5, line:6:3> 'int' has_implicit_result_type
  // CHECKYOLO:   |-IntegerLiteral 0x11f85ef10 <line:4:3> 'int' 3
  // CHECKYOLO:   `-CompoundStmt 0x11f85efa0 <col:11, line:6:3>
  // CHECKYOLO:     `-WildcardPatternStmt 0x11f85ef80 <line:5:5, col:10>
  // CHECKYOLO:       `-IntegerLiteral 0x11f85ef60 <col:10> 'int' 0
  //   a match {
  //     _ if (b>0) => {};
  //   };

  struct s {
    int a;
    int b;
  };
  s cond{1,2};
  cond match {
    [1, 2] => cond.a++;
  };

}