// RUN: %python -c "import glob, os; [os.remove(p) for p in glob.glob(r'%S/slow-*.o.tmp')]"
// RUN: timeout 1 %clang @%S/Inputs/slow.argsfile %S/slow.cpp -o %t-ogcg.o || true
// RUN: timeout 1 %clang @%S/Inputs/slow.argsfile %S/slow.cpp -fclangir -fclangir-mem2reg -o %t-cir.o || true
// RUN: %python -c "import glob, sys; sys.exit(0 if not glob.glob(r'%S/slow-*.o.tmp') else 1)"

// Regression test:
// Ensure slow compile probes use explicit temp outputs and do not leave
// slow-*.o.tmp artifacts in clang/test/CIR/slow.

int slow_no_artifact_dummy;
