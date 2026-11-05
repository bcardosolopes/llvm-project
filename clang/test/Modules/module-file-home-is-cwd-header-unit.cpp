// Test that -fmodule-file-home-is-cwd works with C++20 header units.
// Header units have a null Directory, so the ASTWriter must not skip the
// BaseDirectory setup when ModuleFileHomeIsCwd is set.

// UNSUPPORTED: target={{.*}}-zos{{.*}}, target={{.*}}-aix{{.*}}

// RUN: rm -rf %t
// RUN: mkdir -p %t
// RUN: split-file %s %t

// RUN: cd %t
// RUN: %clang_cc1 -std=c++20 -emit-header-unit -xc++-user-header \
// RUN:     -fmodule-file-home-is-cwd hu.h -o %t/hu.pcm

// RUN: llvm-bcanalyzer --dump --disable-histogram %t/hu.pcm | \
// RUN:     FileCheck %s

// Verify MODULE_DIRECTORY is not emitted (paths should be relative to cwd).
// CHECK-NOT: MODULE_DIRECTORY

// Verify that the input file path is relative, not absolute.
// CHECK: <INPUT_FILE {{.*}}/> blob data = 'hu.h'

//--- hu.h
int header_unit_func(int x);
