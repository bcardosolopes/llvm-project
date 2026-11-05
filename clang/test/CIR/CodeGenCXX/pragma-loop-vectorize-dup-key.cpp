// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++11 -fclangir -emit-llvm -o /dev/null %s
//
// Previously crashed with DictionaryAttr duplicate key assertion.
// Fixed by deduplicating vectorize_enable entries when both
// vectorize(enable) and vectorize_width(N>1) are used on the same loop.

void loop1(int *List, int Length) {
  #pragma clang loop vectorize(enable) vectorize_width(1)
  for (int i = 0; i < Length; i++)
    List[i] = i * 2;
}

void loop2(int *List, int Length) {
  #pragma clang loop vectorize(enable) vectorize_width(2)
  for (int i = 0; i < Length; i++)
    List[i] = i * 2;
}

void loop3(int *List, int Length) {
  #pragma clang loop vectorize_width(1)
  for (int i = 0; i < Length; i++)
    List[i] = i * 2;
}

void loop4(int *List, int Length) {
  #pragma clang loop vectorize_width(2)
  for (int i = 0; i < Length; i++)
    List[i] = i * 2;
}
