// The default lives on the option rather than in setLangDefaults, so C++2d
// needs no flag and every mode can be overridden in either direction.

// RUN: %clang -### -c -std=c++2d %s 2>&1 | FileCheck -check-prefix=DEFAULT %s
// DEFAULT: "-std=c++2d"
// DEFAULT-NOT: "-fdo-expressions"
// DEFAULT-NOT: "-fno-do-expressions"

// RUN: %clang -### -c -std=c++17 -fdo-expressions %s 2>&1 | FileCheck -check-prefix=ON %s
// ON: "-fdo-expressions"

// RUN: %clang -### -c -std=c++2d -fno-do-expressions %s 2>&1 | FileCheck -check-prefix=OFF %s
// OFF: "-fno-do-expressions"

// The last one wins, both ways round.
// RUN: %clang -### -c -std=c++17 -fdo-expressions -fno-do-expressions %s 2>&1 \
// RUN:   | FileCheck -check-prefix=LAST-OFF %s
// LAST-OFF: "-fno-do-expressions"
// LAST-OFF-NOT: "-fdo-expressions"

// RUN: %clang -### -c -std=c++2d -fno-do-expressions -fdo-expressions %s 2>&1 \
// RUN:   | FileCheck -check-prefix=LAST-ON %s
// LAST-ON: "-fdo-expressions"
// LAST-ON-NOT: "-fno-do-expressions"
