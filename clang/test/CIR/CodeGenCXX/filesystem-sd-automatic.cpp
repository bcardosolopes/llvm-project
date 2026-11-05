// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir -std=c++17
// REQUIRES: system-headers
//
// Requires system headers not available in lit (-nostdsysteminc).
// Previously crashed with SD_Automatic not implemented, now compiles with full system headers.
//

#include <filesystem>

void test() {
  namespace fs = std::filesystem;

  // This triggers SD_Automatic for the directory_iterator temporary
  for (const auto& entry : fs::directory_iterator("/tmp")) {
    auto path = entry.path();
  }
}
