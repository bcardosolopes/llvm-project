// Driver plumbing for -fclangir-lto: the link job runs cir-link-wrapper
// instead of the linker, and the wrapper is told what it cannot work out for
// itself.
//
// RUN: %clang -### --target=aarch64-unknown-linux-android -fclangir-lto \
// RUN:     -shared %s -o %t.so 2>&1 | FileCheck %s --check-prefix=SUBST

// The wrapper stands in for the linker, and the real linker is handed to it
// rather than lost.
// SUBST: cir-link-wrapper
// SUBST-SAME: "--linker-path=
// SUBST-SAME: "--cir-triple=aarch64-unknown-linux-android"

// Neither a clang nor an optimization level is passed any more. The wrapper
// compiles nothing: it hands the merged module to the linker as bitcode and
// the linker's own LTO optimizes it, so there is no level for the driver to
// carry over and no compiler for it to name.
// RUN: %clang -### --target=aarch64-unknown-linux-android -fclangir-lto -O0 \
// RUN:     -shared %s -o %t.so 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NOCOMPILE \
// RUN:       --implicit-check-not=--cir-opt-level= \
// RUN:       --implicit-check-not=--cir-clang-path= \
// RUN:       --implicit-check-not=--cir-reloc-model=
// NOCOMPILE: cir-link-wrapper

// Without the flag nothing is substituted -- this is what makes it safe to
// leave on a toolchain that has no CIR in it.
// RUN: %clang -### --target=aarch64-unknown-linux-android -shared %s -o %t.so \
// RUN:     2>&1 | FileCheck %s --check-prefix=OFF
// OFF-NOT: cir-link-wrapper

// -fno-clangir-lto wins over an earlier -fclangir-lto.
// RUN: %clang -### --target=aarch64-unknown-linux-android -fclangir-lto \
// RUN:     -fno-clangir-lto -shared %s -o %t.so 2>&1 \
// RUN:     | FileCheck %s --check-prefix=NEGATED
// NEGATED-NOT: cir-link-wrapper

// Passing it to a compile-only run must not warn: the flag belongs to a set
// used for both compiling and linking.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir-lto -c %s \
// RUN:     -o %t.o -Werror=unused-command-line-argument

// GetProgramPath returns its argument unchanged when it finds nothing, so a
// wrapper that is not beside clang turns into a bare name and fails at exec
// as "posix_spawn: No such file or directory". Under a build system that
// gives every binary its own output directory that is the normal case, so the
// path can be named.
// RUN: %clang -### --target=aarch64-unknown-linux-android -fclangir-lto \
// RUN:     -fclangir-lto-wrapper=/wrapper/elsewhere/cir-link-wrapper \
// RUN:     -shared %s -o %t.so 2>&1 | FileCheck %s --check-prefix=WRAPPER
// WRAPPER: "/wrapper/elsewhere/cir-link-wrapper"

// The wrapper then runs cir-link, cir-translate and clang, and looks each up
// beside itself -- so it has the same problem one level down, and
// -Xclangir-lto is how the build system answers it without the driver
// learning an option per tool.
//
// The forwarded arguments have to land before the linker arguments, or the
// wrapper's own option parsing never sees them and passes them to the linker.
// RUN: %clang -### --target=aarch64-unknown-linux-android -fclangir-lto \
// RUN:     -Xclangir-lto --cir-link-path=/tools/cir-link \
// RUN:     -Xclangir-lto --cir-translate-path=/tools/cir-translate \
// RUN:     -shared %s -o %t.so 2>&1 | FileCheck %s --check-prefix=XARGS
// XARGS: cir-link-wrapper
// XARGS-SAME: "--linker-path=
// XARGS-SAME: "--cir-link-path=/tools/cir-link"
// XARGS-SAME: "--cir-translate-path=/tools/cir-translate"
// The linker's own arguments start here, so the two above are inside the
// region the wrapper parses rather than in the tail it forwards.
// XARGS-SAME: "-EL"

// Neither is worth a diagnostic on a link that is not running the pipeline,
// but they must not be silently dropped either -- so they are unused-argument
// clean the same way -fclangir-lto is.
// RUN: %clang --target=aarch64-unknown-linux-android -c %s -o %t.o \
// RUN:     -fclangir-lto-wrapper=/wrapper/elsewhere/cir-link-wrapper \
// RUN:     -Xclangir-lto --cir-link-path=/tools/cir-link \
// RUN:     -Werror=unused-command-line-argument

int f(void) { return 0; }
