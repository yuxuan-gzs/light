// preprocess.cpp - minimal C++ preprocessor for LangBridge.
//
// Handles the directive subset that appears in real-world C++ sources and
// that materially affects translation quality:
//   * object-like and function-like #define / #undef with token-level
//     expansion (hideset-guarded, recursion-safe)
//   * conditional groups: #if / #ifdef / #ifndef / #elif / #else / #endif
//     with a small constant-expression evaluator (defined(), &&, ||, !,
//     comparisons, shifts, integer arithmetic)
//   * #include collection (kept verbatim for the generators)
//   * #pragma silently dropped; unknown directives reported as warnings
//
// The pass is line-oriented and *preserves line numbering*: every processed
// or skipped physical line is blanked rather than removed, so token line
// numbers still match the original source.
#include "bridge.hpp"

#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

namespace bridge {

namespace {

struct Macro {
  bool func = false;                // function-like macro
  std::vector<std::string> params;  // parameter names (function-like)
  std::vector<Token> body;          // replacement token list
};

// A small builtin table of Win32/CRT macros. Sources translated by LangBridge
// routinely reference windows.h constants; expanding them keeps the generated
// Java self-consistent instead of emitting bare undefined identifiers.
// File-local #defines always take precedence over these.
const std::map<std::string, std::string>& builtinMacros() {
  static const std::map<std::string, std::string> m = {
      // console text attributes
      {"FOREGROUND_BLUE", "0x0001"},
      {"FOREGROUND_GREEN", "0x0002"},
      {"FOREGROUND_RED", "0x0004"},
      {"FOREGROUND_INTENSITY", "0x0008"},
      {"BACKGROUND_BLUE", "0x0010"},
      {"BACKGROUND_GREEN", "0x0020"},
      {"BACKGROUND_RED", "0x0040"},
      {"BACKGROUND_INTENSITY", "0x0080"},
      // booleans / null (kept as literals so Java output stays well-typed)
      {"TRUE", "true"},
      {"FALSE", "false"},
      {"NULL", "nullptr"},
      // std handles
      {"STD_INPUT_HANDLE", "-10"},
      {"STD_OUTPUT_HANDLE", "-11"},
      {"STD_ERROR_HANDLE", "-12"},
      {"INVALID_HANDLE_VALUE", "-1"},
      {"MAX_PATH", "260"},
      // messages
      {"WM_CREATE", "0x0001"},
      {"WM_DESTROY", "0x0002"},
      {"WM_SIZE", "0x0005"},
      {"WM_PAINT", "0x000F"},
      {"WM_CLOSE", "0x0010"},
      {"WM_QUIT", "0x0012"},
      {"WM_KEYDOWN", "0x0100"},
      {"WM_KEYUP", "0x0101"},
      {"WM_CHAR", "0x0102"},
      {"WM_COMMAND", "0x0111"},
      {"WM_TIMER", "0x0113"},
      {"WM_NOTIFY", "0x004E"},
      {"WM_COPYDATA", "0x004A"},
      {"WM_DROPFILES", "0x0233"},
      {"WM_MOUSEMOVE", "0x0200"},
      {"WM_LBUTTONDOWN", "0x0201"},
      {"WM_LBUTTONUP", "0x0202"},
      {"WM_MOUSEWHEEL", "0x020A"},
      {"WM_APP", "0x8000"},
      // window styles
      {"WS_OVERLAPPED", "0x00000000"},
      {"WS_POPUP", "0x80000000"},
      {"WS_CHILD", "0x40000000"},
      {"WS_VISIBLE", "0x10000000"},
      {"WS_CAPTION", "0x00C00000"},
      {"WS_BORDER", "0x00800000"},
      {"WS_SYSMENU", "0x00080000"},
      {"WS_VSCROLL", "0x00200000"},
      {"WS_HSCROLL", "0x00100000"},
      {"WS_EX_TOPMOST", "0x00000008"},
      {"WS_EX_ACCEPTFILES", "0x00000010"},
      {"WS_EX_TOOLWINDOW", "0x00000080"},
      {"WS_EX_APPWINDOW", "0x00040000"},
      {"WS_EX_CLIENTEDGE", "0x00000200"},
      {"CW_USEDEFAULT", "-2147483648"},
      {"SWP_NOSIZE", "0x0001"},
      {"SWP_NOMOVE", "0x0002"},
      {"SWP_NOZORDER", "0x0004"},
      {"SWP_NOACTIVATE", "0x0010"},
      {"SWP_SHOWWINDOW", "0x0040"},
      {"HWND_TOP", "0"},
      {"HWND_TOPMOST", "-1"},
      {"HWND_NOTOPMOST", "-2"},
      {"GWL_STYLE", "-16"},
      {"GWL_EXSTYLE", "-20"},
      {"GA_ROOT", "2"},
      // common controls / dialogs
      {"MB_OK", "0x00000000"},
      {"MB_OKCANCEL", "0x00000001"},
      {"MB_YESNO", "0x00000004"},
      {"MB_ICONERROR", "0x00000010"},
      {"MB_ICONWARNING", "0x00000030"},
      {"MB_ICONQUESTION", "0x00000020"},
      {"MB_ICONINFORMATION", "0x00000040"},
      {"IDOK", "1"},
      {"IDCANCEL", "2"},
      {"IDYES", "6"},
      {"IDNO", "7"},
      {"SW_HIDE", "0"},
      {"SW_SHOWNORMAL", "1"},
      {"SW_SHOW", "5"},
      {"COLOR_WINDOW", "5"},
      {"COLOR_BTNFACE", "15"},
      {"DT_LEFT", "0x00000000"},
      {"DT_CENTER", "0x00000001"},
      {"DT_RIGHT", "0x00000002"},
      {"DT_VCENTER", "0x00000004"},
      {"DT_SINGLELINE", "0x00000020"},
      {"BST_CHECKED", "0x0001"},
      {"BST_UNCHECKED", "0x0000"},
      {"BM_SETCHECK", "0x00F1"},
      {"BM_GETCHECK", "0x00F0"},
      {"LVS_EX_FULLROWSELECT", "0x00000020"},
      // virtual keys
      {"VK_BACK", "0x08"},
      {"VK_TAB", "0x09"},
      {"VK_RETURN", "0x0D"},
      {"VK_ESCAPE", "0x1B"},
      {"VK_SPACE", "0x20"},
      {"VK_DELETE", "0x2E"},
      {"VK_F5", "0x74"},
      // files / processes / registry
      {"GENERIC_READ", "0x80000000"},
      {"GENERIC_WRITE", "0x40000000"},
      {"GENERIC_ALL", "0x10000000"},
      {"FILE_SHARE_READ", "0x00000001"},
      {"FILE_SHARE_WRITE", "0x00000002"},
      {"CREATE_NEW", "1"},
      {"CREATE_ALWAYS", "2"},
      {"OPEN_EXISTING", "3"},
      {"OPEN_ALWAYS", "4"},
      {"TRUNCATE_EXISTING", "5"},
      {"INVALID_SET_FILE_POINTER", "-1"},
      {"HEAP_ZERO_MEMORY", "0x00000008"},
      {"PROCESS_QUERY_INFORMATION", "0x0400"},
      {"PROCESS_VM_READ", "0x0010"},
      {"TH32CS_SNAPPROCESS", "0x00000002"},
      {"HKEY_CLASSES_ROOT", "0x80000000"},
      {"HKEY_CURRENT_USER", "0x80000001"},
      {"HKEY_LOCAL_MACHINE", "0x80000002"},
      {"HKEY_USERS", "0x80000003"},
      {"KEY_READ", "0x20019"},
      {"KEY_WRITE", "0x20006"},
      {"KEY_ALL_ACCESS", "0xF003F"},
      {"REG_SZ", "1"},
      {"REG_DWORD", "4"},
      // waits / code pages
      {"INFINITE", "0xFFFFFFFF"},
      {"WAIT_OBJECT_0", "0x00000000"},
      {"WAIT_TIMEOUT", "0x00000102"},
      {"CP_UTF8", "65001"},
      // HRESULTs
      {"S_OK", "0"},
      {"S_FALSE", "1"},
      {"E_FAIL", "-2147467259"},
      {"E_INVALIDARG", "-2147024809"},
      {"E_POINTER", "-2147467261"},
      {"E_NOTIMPL", "-2147467263"},
      {"E_NOINTERFACE", "-2147467262"},
      {"E_ACCESSDENIED", "-2147024891"},
      {"E_OUTOFMEMORY", "-2147024882"},
  };
  return m;
}

// Function-like builtin macros: name -> (params, body text).
const std::map<std::string, std::pair<std::vector<std::string>, std::string>>&
builtinFuncMacros() {
  static const std::map<std::string, std::pair<std::vector<std::string>, std::string>> m = {
      {"RGB", {{"r", "g", "b"}, "((r) | ((g) << 8) | ((b) << 16))"}},
      {"MAKEWORD", {{"a", "b"}, "((a) | ((b) << 8))"}},
      {"MAKELONG", {{"a", "b"}, "((a) | ((b) << 16))"}},
      {"LOWORD", {{"l"}, "((l) & 0xFFFF)"}},
      {"HIWORD", {{"l"}, "(((l) >> 16) & 0xFFFF)"}},
  };
  return m;
}

std::string trimStr(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::string firstWord(const std::string& s, std::string& rest) {
  std::string t = trimStr(s);
  size_t i = 0;
  while (i < t.size() && (std::isalnum(static_cast<unsigned char>(t[i])) || t[i] == '_')) ++i;
  std::string w = t.substr(0, i);
  rest = trimStr(t.substr(i));
  return w;
}

// ---------------------------------------------------------------------------
// Conditional expression evaluator (integer subset of the C preprocessor)
// ---------------------------------------------------------------------------
struct Ev {
  const std::vector<Token>& t;
  size_t i = 0;
  bool fail = false;

  explicit Ev(const std::vector<Token>& toks) : t(toks) {}

  // The lexer appends a terminating Tok::End; it must not look like a real
  // token here, otherwise parse() would report trailing garbage for every
  // condition and every #if would evaluate to false.
  const Token* cur() {
    size_t j = i;
    while (j < t.size() && t[j].kind == Tok::Eol) ++j;
    if (j >= t.size() || t[j].kind == Tok::End) return nullptr;
    return &t[j];
  }
  void bump() {
    while (i < t.size() && t[i].kind == Tok::Eol) ++i;
    if (i < t.size() && t[i].kind != Tok::End) ++i;
  }
  bool isOp(const char* o) {
    const Token* c = cur();
    return c && c->kind == Tok::Op && c->text == o;
  }
  bool acceptOp(const char* o) {
    if (isOp(o)) { bump(); return true; }
    return false;
  }

  long long parse() {
    long long v = lor();
    if (cur()) fail = true;  // trailing garbage
    return v;
  }

  long long lor() {
    long long v = land();
    while (isOp("||")) { bump(); long long r = land(); v = (v || r) ? 1 : 0; }
    return v;
  }
  long long land() {
    long long v = bor();
    while (isOp("&&")) { bump(); long long r = bor(); v = (v && r) ? 1 : 0; }
    return v;
  }
  long long bor() {
    long long v = bxor();
    while (isOp("|")) { bump(); v |= bxor(); }
    return v;
  }
  long long bxor() {
    long long v = band();
    while (isOp("^")) { bump(); v ^= band(); }
    return v;
  }
  long long band() {
    long long v = eq();
    while (isOp("&")) { bump(); v &= eq(); }
    return v;
  }
  long long eq() {
    long long v = rel();
    while (true) {
      if (isOp("==")) { bump(); v = (v == rel()) ? 1 : 0; }
      else if (isOp("!=")) { bump(); v = (v != rel()) ? 1 : 0; }
      else break;
    }
    return v;
  }
  long long rel() {
    long long v = shift();
    while (true) {
      if (isOp("<")) { bump(); v = (v < shift()) ? 1 : 0; }
      else if (isOp("<=")) { bump(); v = (v <= shift()) ? 1 : 0; }
      else if (isOp(">")) {
        // '>>' was split by the lexer; a '>' directly followed by another
        // '>' inside a condition is a right shift.
        bump();
        if (isOp(">")) { bump(); v = v >> shift(); }
        else v = (v > shift()) ? 1 : 0;
      }
      else if (isOp(">=")) { bump(); v = (v >= shift()) ? 1 : 0; }
      else break;
    }
    return v;
  }
  long long shift() {
    long long v = add();
    while (true) {
      if (isOp("<<")) { bump(); v <<= add(); }
      else break;
    }
    return v;
  }
  long long add() {
    long long v = mul();
    while (true) {
      if (isOp("+")) { bump(); v += mul(); }
      else if (isOp("-")) { bump(); v -= mul(); }
      else break;
    }
    return v;
  }
  long long mul() {
    long long v = unary();
    while (true) {
      if (isOp("*")) { bump(); v *= unary(); }
      else if (isOp("/")) {
        bump();
        long long r = unary();
        if (r == 0) { fail = true; return 0; }
        v /= r;
      }
      else if (isOp("%")) {
        bump();
        long long r = unary();
        if (r == 0) { fail = true; return 0; }
        v %= r;
      }
      else break;
    }
    return v;
  }
  long long unary() {
    if (isOp("!")) { bump(); return unary() == 0 ? 1 : 0; }
    if (isOp("-")) { bump(); return -unary(); }
    if (isOp("+")) { bump(); return unary(); }
    if (isOp("~")) { bump(); return ~unary(); }
    return primary();
  }
  long long primary() {
    const Token* c = cur();
    if (!c) { fail = true; return 0; }
    if (c->kind == Tok::Int) {
      std::string txt = c->text;
      // strip integer suffixes so strtoll accepts the literal
      while (!txt.empty() && (txt.back() == 'u' || txt.back() == 'U' ||
                              txt.back() == 'l' || txt.back() == 'L'))
        txt.pop_back();
      bump();
      return std::strtoll(txt.c_str(), nullptr, 0);
    }
    if (c->kind == Tok::Char) {
      // char literal in a condition: use the first payload byte (the payload
      // is the inner text kept verbatim by the lexer)
      bump();
      const std::string& s = c->text;
      if (s.empty()) return 0;
      if (s[0] == '\\' && s.size() >= 2) {
        switch (s[1]) {
          case 'n': return '\n';
          case 't': return '\t';
          case 'r': return '\r';
          case '0': return 0;
          default: return static_cast<unsigned char>(s[1]);
        }
      }
      return static_cast<unsigned char>(s[0]);
    }
    if (c->kind == Tok::Op && c->text == "(") {
      bump();
      long long v = lor();
      if (!acceptOp(")")) fail = true;
      return v;
    }
    // Word: after 'defined' processing and macro expansion a remaining word
    // is an undefined identifier, which evaluates to 0 in C.
    if (c->kind == Tok::Word) {
      bump();
      return 0;
    }
    fail = true;
    return 0;
  }
};

// Replace 'defined X' / 'defined(X)' with 1/0 literals *before* macro
// expansion, as the C standard requires.
std::vector<Token> resolveDefined(const std::vector<Token>& in,
                                  const std::function<bool(const std::string&)>& isDef) {
  std::vector<Token> out;
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i].kind == Tok::Word && in[i].text == "defined") {
      size_t j = i + 1;
      while (j < in.size() && in[j].kind == Tok::Eol) ++j;
      bool haveName = false;
      std::string name;
      if (j < in.size() && in[j].kind == Tok::Op && in[j].text == "(") {
        // 'defined(NAME)' - the parenthesised form.
        size_t k = j + 1;
        while (k < in.size() && in[k].kind == Tok::Eol) ++k;
        if (k < in.size() && in[k].kind == Tok::Word) {
          name = in[k].text;
          haveName = true;
          ++k;
          while (k < in.size() && in[k].kind == Tok::Eol) ++k;
          if (k < in.size() && in[k].kind == Tok::Op && in[k].text == ")") ++k;
          j = k;
        }
      } else if (j < in.size() && in[j].kind == Tok::Word) {
        // 'defined NAME' - a following '(' is *not* part of the operator.
        name = in[j].text;
        haveName = true;
        ++j;
      }
      if (haveName) {
        Token lit;
        lit.kind = Tok::Int;
        lit.text = isDef(name) ? "1" : "0";
        lit.line = in[i].line;
        out.push_back(lit);
        i = j - 1;
        continue;
      }
    }
    out.push_back(in[i]);
  }
  return out;
}

// ---------------------------------------------------------------------------
// Preprocessor
// ---------------------------------------------------------------------------
class Pre {
public:
  explicit Pre(std::vector<Diag>& diags) : diags_(diags) {}

  PpResult run(const std::string& text) {
    PpResult res;
    const bool crlf = text.find("\r\n") != std::string::npos;
    std::vector<std::string> lines = splitLines(text);
    std::vector<std::string> out(lines.size());

    // ---- pass 1: directives + conditional regions ----
    bool inBlock = false;
    for (size_t li = 0; li < lines.size(); ++li) {
      std::string line = stripCr(lines[li]);
      if (inBlock) {
        size_t end = line.find("*/");
        if (end == std::string::npos) { out[li] = ""; continue; }
        inBlock = false;
        line = line.substr(end + 2);
      }
      // strip line comments (outside string/char literals) and detect the
      // start of a block comment that stays open on this line
      std::string code = maskComments(line, inBlock);
      std::string t = trimStr(code);
      if (!t.empty() && t[0] == '#') {
        handleDirective(t, li + 1);
        out[li] = "";
        continue;
      }
      out[li] = active() ? code : "";
    }

    // ---- pass 2: token-level macro expansion on the kept lines ----
    std::string kept = joinLines(out, crlf);
    std::vector<Token> toks;
    try {
      toks = lex(kept);
    } catch (const ParseError& e) {
      Diag d;
      d.line = e.line;
      d.msg = std::string("preprocessed source failed to lex: ") + e.what();
      d.warning = true;
      diags_.push_back(d);
      res.text = text;  // fall back to the raw source
      res.includes = includes_;
      return res;
    }
    std::set<std::string> hide;
    std::vector<Token> expanded = expand(toks, hide, 0);

    // Re-serialize tokens back to text (line numbers preserved via Eol
    // tokens). This keeps the parser input free of macro noise while
    // remaining debuggable.
    res.text = serialize(expanded);
    res.includes = includes_;
    return res;
  }

private:
  std::vector<Diag>& diags_;
  std::map<std::string, Macro> macros_;
  std::vector<std::string> includes_;

  struct Cond {
    bool parentActive;
    bool taken;   // some branch of this group has been taken
    bool active;  // current branch emits tokens
  };
  std::vector<Cond> conds_;

  bool active() const { return conds_.empty() ? true : conds_.back().active; }

  static std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
      if (c == '\n') { lines.push_back(cur); cur.clear(); }
      else cur.push_back(c);
    }
    lines.push_back(cur);
    return lines;
  }

  static std::string stripCr(const std::string& s) {
    if (!s.empty() && s.back() == '\r') return s.substr(0, s.size() - 1);
    return s;
  }

  static std::string joinLines(const std::vector<std::string>& lines, bool crlf) {
    std::string nl = crlf ? "\r\n" : "\n";
    std::string r;
    for (size_t i = 0; i < lines.size(); ++i) {
      if (i) r += nl;
      r += lines[i];
    }
    return r;
  }

  // Blank out comments (and code following an unterminated block comment).
  // 'inBlock' is updated when a block comment stays open past end of line.
  static std::string maskComments(const std::string& line, bool& inBlock) {
    std::string out;
    bool inStr = false, inChr = false;
    for (size_t i = 0; i < line.size(); ++i) {
      char c = line[i];
      if (inStr) {
        out.push_back(c);
        if (c == '\\' && i + 1 < line.size()) { out.push_back(line[++i]); continue; }
        if (c == '"') inStr = false;
        continue;
      }
      if (inChr) {
        out.push_back(c);
        if (c == '\\' && i + 1 < line.size()) { out.push_back(line[++i]); continue; }
        if (c == '\'') inChr = false;
        continue;
      }
      if (c == '"') { inStr = true; out.push_back(c); continue; }
      if (c == '\'') { inChr = true; out.push_back(c); continue; }
      if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') break;  // line comment
      if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
        size_t end = line.find("*/", i + 2);
        if (end == std::string::npos) {
          inBlock = true;
          break;  // drop the rest of the line
        }
        i = end + 1;
        continue;
      }
      out.push_back(c);
    }
    return out;
  }

  // ---- macro table ----
  const Macro* lookupMacro(const std::string& name) const {
    auto it = macros_.find(name);
    if (it != macros_.end()) return &it->second;
    static std::map<std::string, Macro> builtin;
    if (builtin.empty()) {
      for (auto& kv : builtinMacros()) {
        Macro m;
        m.body = lexBody(kv.second, kv.first);
        if (!m.body.empty() || kv.second.empty()) builtin[kv.first] = m;
      }
      for (auto& kv : builtinFuncMacros()) {
        Macro m;
        m.func = true;
        m.params = kv.second.first;
        m.body = lexBody(kv.second.second, kv.first);
        builtin[kv.first] = m;
      }
    }
    auto bit = builtin.find(name);
    return bit == builtin.end() ? nullptr : &bit->second;
  }

  // Lex a macro replacement list. The trailing End token from lex() must be
  // stripped: macro bodies are spliced into the token stream, and a stray
  // End would truncate the rest of the file during re-serialization.
  static std::vector<Token> lexBody(const std::string& text, const std::string& what) {
    (void)what;
    std::vector<Token> toks;
    try {
      toks = lex(text);
    } catch (const ParseError&) {
      return {};
    }
    while (!toks.empty() && toks.back().kind == Tok::End) toks.pop_back();
    return toks;
  }

  // ---- directives ----
  void handleDirective(const std::string& trimmed, int line) {
    std::string rest;
    std::string name = firstWord(trimmed.substr(1), rest);  // skip '#'

    // Conditional directives are tracked even inside inactive regions.
    if (name == "if" || name == "ifdef" || name == "ifndef") {
      bool parentActive = active();
      bool val = false;
      if (parentActive) {
        if (name == "if") {
          val = evalCond(rest, line);
        } else {
          std::string id = firstWord(rest, rest);
          bool def = lookupMacro(id) != nullptr;
          val = (name == "ifdef") ? def : !def;
        }
      }
      conds_.push_back({parentActive, val, parentActive && val});
      return;
    }
    if (name == "elif") {
      if (conds_.empty()) { warnLine(line, "#elif without #if"); return; }
      Cond& c = conds_.back();
      if (c.active) { c.active = false; return; }
      if (c.taken || !c.parentActive) { c.active = false; return; }
      bool val = evalCond(rest, line);
      c.active = val;
      if (val) c.taken = true;
      return;
    }
    if (name == "else") {
      if (conds_.empty()) { warnLine(line, "#else without #if"); return; }
      Cond& c = conds_.back();
      if (c.active) { c.active = false; return; }
      if (c.taken || !c.parentActive) { c.active = false; return; }
      c.active = true;
      c.taken = true;
      return;
    }
    if (name == "endif") {
      if (conds_.empty()) { warnLine(line, "#endif without #if"); return; }
      conds_.pop_back();
      return;
    }

    // Everything below is ignored inside inactive regions.
    if (!active()) return;

    if (name == "include") {
      std::string t = trimStr(rest);
      if (!t.empty()) includes_.push_back("#include " + t);
      return;
    }
    if (name == "pragma") return;  // silently dropped (comment(lib), once, pack...)
    if (name == "define") {
      defineMacro(rest, line);
      return;
    }
    if (name == "undef") {
      std::string id = firstWord(rest, rest);
      if (!id.empty()) macros_.erase(id);
      return;
    }
    if (name.empty()) return;  // null directive '#'
    warnLine(line, "preprocessor directive '" + name + "' ignored");
  }

  void defineMacro(const std::string& rest, int line) {
    std::string t = trimStr(rest);
    if (t.empty()) { warnLine(line, "#define without a name"); return; }
    size_t i = 0;
    if (!(std::isalpha(static_cast<unsigned char>(t[0])) || t[0] == '_')) {
      warnLine(line, "unsupported #define target");
      return;
    }
    while (i < t.size() && (std::isalnum(static_cast<unsigned char>(t[i])) || t[i] == '_')) ++i;
    std::string macroName = t.substr(0, i);
    Macro m;
    if (i < t.size() && t[i] == '(') {
      // function-like: '(' immediately after the name, no whitespace
      size_t j = i + 1;
      std::string params;
      int depth = 1;
      while (j < t.size() && depth > 0) {
        if (t[j] == '(') ++depth;
        else if (t[j] == ')') { --depth; if (depth == 0) break; }
        params.push_back(t[j]);
        ++j;
      }
      if (j >= t.size()) { warnLine(line, "malformed #define parameter list"); return; }
      ++j;  // past ')'
      m.func = true;
      for (auto& p : splitStr(params, ',')) {
        std::string pn = trimStr(p);
        if (!pn.empty()) m.params.push_back(pn);
      }
      m.body = lexBody(trimStr(t.substr(j)), macroName);
    } else {
      m.body = lexBody(trimStr(t.substr(i)), macroName);
    }
    macros_[macroName] = m;
  }

  bool evalCond(const std::string& expr, int line) {
    std::vector<Token> toks;
    try {
      toks = lex(expr);
    } catch (const ParseError&) {
      warnLine(line, "unparsable #if condition; treated as false");
      return false;
    }
    auto isDef = [this](const std::string& n) { return lookupMacro(n) != nullptr; };
    toks = resolveDefined(toks, isDef);
    std::set<std::string> hide;
    toks = expand(toks, hide, 0);
    Ev ev(toks);
    long long v = ev.parse();
    if (ev.fail) {
      warnLine(line, "unparsable #if condition; treated as false");
      return false;
    }
    return v != 0;
  }

  void warnLine(int line, const std::string& msg) {
    Diag d;
    d.line = line;
    d.msg = msg;
    d.warning = true;
    diags_.push_back(d);
  }

  // ---- token-level expansion ----
  // Collect macro call arguments starting at in[open] == '('. Returns false
  // when the parentheses are unbalanced (then the name is not a call).
  bool collectArgs(const std::vector<Token>& in, size_t open,
                   std::vector<std::vector<Token>>& args,
                   size_t* closePos = nullptr) {
    int depth = 1;
    std::vector<Token> cur;
    size_t i = open + 1;
    for (; i < in.size(); ++i) {
      const Token& t = in[i];
      if (t.kind == Tok::Eol) continue;
      if (t.kind == Tok::Op && t.text == "(") { ++depth; cur.push_back(t); continue; }
      if (t.kind == Tok::Op && t.text == ")") {
        --depth;
        if (depth == 0) {
          args.push_back(cur);
          if (closePos) *closePos = i;
          return true;
        }
        cur.push_back(t);
        continue;
      }
      if (t.kind == Tok::Op && t.text == "," && depth == 1) {
        args.push_back(cur);
        cur.clear();
        continue;
      }
      cur.push_back(t);
    }
    return false;
  }

  std::string tokenText(const std::vector<Token>& toks) {
    std::string s;
    for (size_t i = 0; i < toks.size(); ++i) {
      if (i) s += " ";
      s += toks[i].text;
    }
    return s;
  }

  // Substitute parameters into a macro body (handles '#' stringize and '##'
  // token pasting in a simplified form).
  std::vector<Token> substMacro(const Macro& m,
                                const std::vector<std::vector<Token>>& args,
                                const std::set<std::string>& hide, int depth) {
    std::vector<Token> out;
    for (size_t i = 0; i < m.body.size(); ++i) {
      const Token& t = m.body[i];
      if (t.kind == Tok::Op && t.text == "#" && i + 1 < m.body.size() &&
          m.body[i + 1].kind == Tok::Word) {
        const std::string& pn = m.body[i + 1].text;
        for (size_t p = 0; p < m.params.size(); ++p) {
          if (m.params[p] == pn) {
            Token s;
            s.kind = Tok::Str;
            s.text = tokenText(p < args.size() ? args[p] : std::vector<Token>{});
            s.line = t.line;
            out.push_back(s);
            ++i;
            break;
          }
        }
        continue;
      }
      if (t.kind == Tok::Op && t.text == "##") {
        // paste: merge the next token onto the previous one
        if (i + 1 < m.body.size() && !out.empty()) {
          out.back().text += m.body[i + 1].text;
          ++i;
        }
        continue;
      }
      bool isParam = false;
      if (t.kind == Tok::Word) {
        for (size_t p = 0; p < m.params.size(); ++p) {
          if (m.params[p] == t.text) {
            isParam = true;
            if (p < args.size()) {
              std::vector<Token> ex = expand(args[p], hide, depth);
              for (auto& a : ex) {
                Token c = a;
                c.line = t.line;
                out.push_back(c);
              }
            }
            break;
          }
        }
      }
      if (!isParam) out.push_back(t);
    }
    return out;
  }

  std::vector<Token> expand(const std::vector<Token>& in, const std::set<std::string>& hide,
                            int depth) {
    std::vector<Token> out;
    if (depth > 64) return in;  // runaway recursion guard
    size_t i = 0;
    while (i < in.size()) {
      const Token& t = in[i];
      if (t.kind == Tok::Word && !hide.count(t.text)) {
        const Macro* m = lookupMacro(t.text);
        if (m) {
          if (!m->func) {
            std::set<std::string> h2 = hide;
            h2.insert(t.text);
            std::vector<Token> body = expand(m->body, h2, depth + 1);
            for (auto& b : body) {
              Token c = b;
              c.line = t.line;
              out.push_back(c);
            }
            ++i;
            continue;
          }
          // function-like macro: expands only when followed by '('
          size_t j = i + 1;
          while (j < in.size() && in[j].kind == Tok::Eol) ++j;
          if (j < in.size() && in[j].kind == Tok::Op && in[j].text == "(") {
            std::vector<std::vector<Token>> args;
            size_t closeIdx = j;
            if (collectArgs(in, j, args, &closeIdx) &&
                (!m->params.empty() || !args.empty())) {
              std::set<std::string> h2 = hide;
              h2.insert(t.text);
              std::vector<Token> sub = substMacro(*m, args, h2, depth);
              std::vector<Token> body = expand(sub, h2, depth + 1);
              for (auto& b : body) {
                Token c = b;
                c.line = t.line;
                out.push_back(c);
              }
              i = closeIdx + 1;  // continue past the closing ')'
              continue;
            }
          }
        }
      }
      out.push_back(t);
      ++i;
    }
    return out;
  }

  // Re-serialize a token stream back into source text, keeping Eol tokens so
  // the original line numbering survives.
  static std::string serialize(const std::vector<Token>& toks) {
    std::string r;
    bool atLineStart = true;
    for (const Token& t : toks) {
      if (t.kind == Tok::End) break;
      if (t.kind == Tok::Eol) {
        r.push_back('\n');
        atLineStart = true;
        continue;
      }
      if (!atLineStart) r.push_back(' ');
      switch (t.kind) {
        case Tok::Str: r.push_back('"'); r += t.text; r.push_back('"'); break;
        case Tok::Char: r.push_back('\''); r += t.text; r.push_back('\''); break;
        default: r += t.text; break;
      }
      atLineStart = false;
    }
    if (!r.empty() && r.back() != '\n') r.push_back('\n');
    return r;
  }
};

}  // namespace

PpResult preprocessCpp(const std::string& text, std::vector<Diag>& diags) {
  Pre pre(diags);
  return pre.run(text);
}

}  // namespace bridge
