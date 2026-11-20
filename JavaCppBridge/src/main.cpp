// langbridge.cpp - LangBridge command-line entry.
//
// Usage:
//   langbridge --mode java2cpp -i Foo.java -o Foo.cpp
//   langbridge --mode cpp2java -i Foo.cpp -o Foo.java
//   langbridge -m java2cpp --stdin --stdout
//
// Reads a whole source file, parses it with the shared front end and emits
// the translated code. Diagnostics (warnings/notes) go to stderr; the exit
// code is 0 on success, 1 for input/parse errors and 2 for CLI misuse.
#include "bridge.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void printUsage(std::ostream& os) {
  os
      << "LangBridge - Java <-> C++ source translator\n"
      << "\n"
      << "Usage:\n"
      << "  langbridge --mode <java2cpp|cpp2java> -i <input> [-o <output>]\n"
      << "  langbridge --mode <java2cpp|cpp2java> --stdin [--stdout]\n"
      << "\n"
      << "Options:\n"
      << "  -m, --mode <mode>     java2cpp | cpp2java (required)\n"
      << "  -i, --input <file>    input source file\n"
      << "  -o, --output <file>   output file (default: stdout / <input-base>.<ext>)\n"
      << "      --stdin           read source from standard input\n"
      << "      --stdout          write result to standard output\n"
      << "  -h, --help            show this help\n"
      << "  -V, --version         print version\n";
}

bool readAll(const std::string& path, std::string& out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool writeAll(const std::string& path, const std::string& data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(f);
}

// True when the buffer is valid UTF-8 (BOM may be present); used to decide
// whether a GBK->UTF-8 conversion is needed for legacy Windows sources.
bool looksLikeUtf8(const std::string& s) {
  size_t i = 0;
  const size_t n = s.size();
  if (n >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
      static_cast<unsigned char>(s[1]) == 0xBB &&
      static_cast<unsigned char>(s[2]) == 0xBF)
    return true;  // BOM: definitely UTF-8
  while (i < n) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      ++i;
      continue;
    }
    int extra = 0;
    if ((c & 0xE0) == 0xC0) extra = 1;
    else if ((c & 0xF0) == 0xE0) extra = 2;
    else if ((c & 0xF8) == 0xF0) extra = 3;
    else return false;
    if (i + extra >= n) return false;
    for (int k = 1; k <= extra; ++k) {
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
    }
    i += static_cast<size_t>(extra) + 1;
  }
  return true;
}

#ifdef _WIN32
// Convert an ANSI/GBK-encoded buffer to UTF-8 (best effort).
std::string ansiToUtf8(const std::string& ansi) {
  if (ansi.empty()) return ansi;
  int wlen = MultiByteToWideChar(CP_ACP, 0, ansi.data(),
                                 static_cast<int>(ansi.size()), nullptr, 0);
  if (wlen <= 0) return ansi;
  std::wstring wstr(wlen, L'\0');
  MultiByteToWideChar(CP_ACP, 0, ansi.data(), static_cast<int>(ansi.size()),
                      &wstr[0], wlen);
  int ulen = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen, nullptr, 0,
                                 nullptr, nullptr);
  if (ulen <= 0) return ansi;
  std::string out(static_cast<size_t>(ulen), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wstr.data(), wlen, &out[0], ulen, nullptr,
                      nullptr);
  return out;
}
#endif

// Normalize input: strip UTF-8 BOM, and if the payload is not valid UTF-8 on
// Windows, assume it is a legacy ANSI/GBK file and convert to UTF-8.
std::string normalizeInput(const std::string& raw) {
  std::string s = raw;
  if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
      static_cast<unsigned char>(s[1]) == 0xBB &&
      static_cast<unsigned char>(s[2]) == 0xBF) {
    s = s.substr(3);
  }
  if (!looksLikeUtf8(s)) {
#ifdef _WIN32
    return ansiToUtf8(s);
#else
    return s;  // unknown encoding; pass through unchanged
#endif
  }
  return s;
}

std::string defaultOutput(const std::string& input, const std::string& mode) {
  const char* targetExt = (mode == "java2cpp") ? ".cpp" : ".java";
  size_t dot = input.find_last_of('.');
  size_t slash = input.find_last_of("/\\");
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
    return input + targetExt;
  }
  return input.substr(0, dot) + targetExt;
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string mode, input, output;
  bool useStdin = false, useStdout = false;

  auto takeValue = [&](int& i, const char* opt) -> std::string {
    if (i + 1 >= argc) {
      std::cerr << "error: option '" << opt << "' requires a value\n";
      std::exit(2);
    }
    return argv[++i];
  };

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-h" || a == "--help") {
      printUsage(std::cout);
      return 0;
    } else if (a == "-V" || a == "--version") {
      std::cout << "LangBridge 1.0.0\n";
      return 0;
    } else if (a == "-m" || a == "--mode") {
      mode = takeValue(i, a.c_str());
    } else if (a == "-i" || a == "--input") {
      input = takeValue(i, a.c_str());
    } else if (a == "-o" || a == "--output") {
      output = takeValue(i, a.c_str());
    } else if (a == "--stdin") {
      useStdin = true;
    } else if (a == "--stdout") {
      useStdout = true;
    } else {
      std::cerr << "error: unknown or misplaced argument '" << a << "'\n";
      printUsage(std::cerr);
      return 2;
    }
  }

  if (mode != "java2cpp" && mode != "cpp2java") {
    std::cerr << "error: --mode must be 'java2cpp' or 'cpp2java'\n";
    return 2;
  }
  if (input.empty() && !useStdin) {
    std::cerr << "error: missing input (-i FILE or --stdin)\n";
    return 2;
  }
  if (!input.empty() && useStdin) {
    std::cerr << "error: -i and --stdin are mutually exclusive\n";
    return 2;
  }
  if (output.empty() && !useStdout) {
    output = useStdin ? std::string("-") : defaultOutput(input, mode);
    if (output == "-") {
      // --stdin without --stdout goes to stdout anyway
      useStdout = true;
    } else if (!input.empty()) {
      std::cerr << "note: output defaults to '" << output << "'\n";
    }
  }

  // ---- Read input ----
  std::string src;
  if (useStdin) {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    src = ss.str();
  } else if (!readAll(input, src)) {
    std::cerr << "error: cannot read input file '" << input << "'\n";
    return 1;
  }
  src = normalizeInput(src);

  // ---- Parse ----
  std::vector<bridge::Diag> diags;
  std::shared_ptr<bridge::Program> prog;
  // Diagnostics are appended by the preprocessor, the parser and the
  // generators; print each one exactly once as it becomes available.
  size_t diagsPrinted = 0;
  auto flushDiags = [&]() {
    for (size_t i = diagsPrinted; i < diags.size(); ++i) {
      if (diags[i].warning)
        std::cerr << "warning(line " << diags[i].line << "): " << diags[i].msg
                  << "\n";
    }
    diagsPrinted = diags.size();
  };
  try {
    prog = bridge::parse(src, diags);
  } catch (const bridge::ParseError& e) {
    flushDiags();
    std::cerr << "error(line " << e.line << "): " << e.what() << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "error: parse failure: " << e.what() << "\n";
    return 1;
  }
  flushDiags();

  // ---- Translate ----
  std::string result;
  try {
    result = (mode == "java2cpp") ? bridge::generateJ2C(prog, diags)
                                  : bridge::generateC2J(prog, diags);
  } catch (const std::exception& e) {
    flushDiags();
    std::cerr << "error: generation failed: " << e.what() << "\n";
    return 1;
  }
  flushDiags();

  // ---- Write output ----
  if (useStdout) {
    std::cout << result;
    if (!result.empty() && result.back() != '\n') std::cout << "\n";
    return 0;
  }
  if (!writeAll(output, result)) {
    std::cerr << "error: cannot write output file '" << output << "'\n";
    return 1;
  }
  std::cerr << "ok: wrote " << output << " (" << result.size() << " bytes)\n";
  return 0;
}