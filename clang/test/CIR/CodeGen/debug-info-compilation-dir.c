// REQUIRES: aarch64-registered-target

// The compile unit's directory is DW_AT_comp_dir, the base symbolizers resolve
// every relative path in the unit against. CIR has to take it from
// -fdebug-compilation-dir (remapped through -fdebug-prefix-map) exactly as
// classic CodeGen does; each case runs both and shares the CHECK lines.

// A relative source path is rooted at the compilation directory.
// RUN: cd %S && %clang_cc1 -triple aarch64-linux-android21 -fclangir \
// RUN:   -emit-llvm -debug-info-kind=limited -fdebug-compilation-dir=. \
// RUN:   -main-file-name debug-info-compilation-dir.c \
// RUN:   debug-info-compilation-dir.c -o - | FileCheck %s --check-prefix=REL
// RUN: cd %S && %clang_cc1 -triple aarch64-linux-android21 \
// RUN:   -emit-llvm -debug-info-kind=limited -fdebug-compilation-dir=. \
// RUN:   -main-file-name debug-info-compilation-dir.c \
// RUN:   debug-info-compilation-dir.c -o - | FileCheck %s --check-prefix=REL

// An absolute source path stays whole; the directory is still recorded.
// RUN: %clang_cc1 -triple aarch64-linux-android21 -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited -fdebug-compilation-dir=%S \
// RUN:   -main-file-name debug-info-compilation-dir.c %s -o - \
// RUN:   | FileCheck %s --check-prefix=ABS -DDIR=%S
// RUN: %clang_cc1 -triple aarch64-linux-android21 -emit-llvm \
// RUN:   -debug-info-kind=limited -fdebug-compilation-dir=%S \
// RUN:   -main-file-name debug-info-compilation-dir.c %s -o - \
// RUN:   | FileCheck %s --check-prefix=ABS -DDIR=%S

// -fdebug-prefix-map applies to both the file name and the directory.
// RUN: %clang_cc1 -triple aarch64-linux-android21 -fclangir -emit-llvm \
// RUN:   -debug-info-kind=limited -fdebug-compilation-dir=%S \
// RUN:   -fdebug-prefix-map=%S=/remapped \
// RUN:   -main-file-name debug-info-compilation-dir.c %s -o - \
// RUN:   | FileCheck %s --check-prefix=MAP
// RUN: %clang_cc1 -triple aarch64-linux-android21 -emit-llvm \
// RUN:   -debug-info-kind=limited -fdebug-compilation-dir=%S \
// RUN:   -fdebug-prefix-map=%S=/remapped \
// RUN:   -main-file-name debug-info-compilation-dir.c %s -o - \
// RUN:   | FileCheck %s --check-prefix=MAP

// The directory reaches the object file as DW_AT_comp_dir.
// RUN: cd %S && %clang_cc1 -triple aarch64-linux-android21 -fclangir \
// RUN:   -emit-obj -debug-info-kind=limited -dwarf-version=5 \
// RUN:   -fdebug-compilation-dir=. \
// RUN:   -main-file-name debug-info-compilation-dir.c \
// RUN:   debug-info-compilation-dir.c -o %t.o
// RUN: llvm-dwarfdump --debug-info %t.o | FileCheck %s --check-prefix=DWARF

int add(int a, int b) { return a + b; }

// REL: !DICompileUnit({{.*}}file: ![[FILE:[0-9]+]]
// REL: ![[FILE]] = !DIFile(filename: "debug-info-compilation-dir.c", directory: ".")

// ABS: !DICompileUnit({{.*}}file: ![[FILE:[0-9]+]]
// ABS: ![[FILE]] = !DIFile(filename: "[[DIR]]/debug-info-compilation-dir.c", directory: "[[DIR]]")

// MAP: !DICompileUnit({{.*}}file: ![[FILE:[0-9]+]]
// MAP: ![[FILE]] = !DIFile(filename: "/remapped/debug-info-compilation-dir.c", directory: "/remapped")

// DWARF: DW_TAG_compile_unit
// DWARF: DW_AT_name ("debug-info-compilation-dir.c")
// DWARF: DW_AT_comp_dir (".")
