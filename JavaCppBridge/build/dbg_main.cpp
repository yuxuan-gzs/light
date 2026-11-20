// Temporary debug harness (not part of the build).
// Usage: dbg.exe <file>  -- dumps preprocessed text.
#include "bridge.hpp"
#include <cstdio>
using namespace bridge;

static std::string slurpStdin() {
  std::string s;
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) s.append(buf, n);
  return s;
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  std::string src = slurpStdin();
  printf("SRC LEN=%zu\n", src.size());
  std::vector<Diag> diags;
  try {
    PpResult pp = preprocessCpp(src, diags);
    printf("PP LEN=%zu\n=== TEXT ===\n%s\n", pp.text.size(), pp.text.c_str());
  } catch (const std::exception& e) {
    printf("PP EXC: %s\n", e.what());
  }
  for (auto& d : diags) printf("DIAG line=%d: %s\n", d.line, d.msg.c_str());
  return 0;
}
