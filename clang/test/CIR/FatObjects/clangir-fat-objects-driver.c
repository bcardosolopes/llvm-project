// RUN: not %clang -target x86_64-unknown-linux-gnu -fclangir-fat-objects -c %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s
// RUN: %clang -target x86_64-unknown-linux-gnu -fclangir -fclangir-fat-objects \
// RUN:   -fno-clangir-fat-objects -### -c %s 2>&1 | FileCheck %s --check-prefix=OFF

// There is nothing to embed if CIR was never in the pipeline, and silently
// producing an ordinary object would leave a later link looking for a section
// that is not there.
// CHECK: error: invalid argument '-fclangir-fat-objects' only allowed with '-fclangir'

// The negation reaches cc1 as an absence, not as a flag.
// OFF-NOT: "-fclangir-fat-objects"
