// parser.cpp - recursive descent parser covering the Java/C++ common subset.
#include "bridge.hpp"
#include <functional>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace bridge {

// ---------------------------------------------------------------------------
// Type helpers
// ---------------------------------------------------------------------------
std::string Type::str() const {
  std::string s;
  if (isConst) s += "const ";
  if (isUnsigned) s += "unsigned ";
  s += base;
  if (!args.empty()) {
    s += "<";
    for (size_t i = 0; i < args.size(); ++i) {
      if (i) s += ",";
      s += args[i].str();
    }
    s += ">";
  }
  for (int i = 0; i < dims; ++i) s += "[]";
  if (isPtr) s += "*";
  if (isRef) s += "&";
  return s;
}

bool Type::operator==(const Type& o) const {
  if (base != o.base || dims != o.dims || isPtr != o.isPtr || isRef != o.isRef ||
      isConst != o.isConst || isUnsigned != o.isUnsigned) return false;
  if (args.size() != o.args.size()) return false;
  for (size_t i = 0; i < args.size(); ++i)
    if (!(args[i] == o.args[i])) return false;
  return true;
}

namespace {

// ---------------------------------------------------------------------------
// Modifier table
// ---------------------------------------------------------------------------
const char* kModifiers[] = {
  "public", "private", "protected", "static", "final", "abstract", "virtual",
  "const", "inline", "constexpr", "override", "synchronized", "transient",
  "volatile", "native", "strictfp", "default", "explicit", "mutable",
  "noexcept", "register", "extern", "constinit", "consteval"
};

bool isModifierWord(const std::string& w) {
  for (const char* m : kModifiers) if (w == m) return true;
  return false;
}

// Calling-convention decorations (Win32/Win64 macros and MSVC extensions).
bool isConvWord(const std::string& w) {
  static const std::set<std::string> s = {
      "CALLBACK", "WINAPI", "APIENTRY", "__stdcall", "__cdecl", "__fastcall",
      "__thiscall", "__vectorcall", "STDMETHODCALLTYPE", "__declspec"};
  return s.count(w) != 0;
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------
struct Parser {
  const std::vector<Token>& toks;
  size_t pos = 0;
  std::vector<Diag>& diags;
  Program prog;
  std::set<std::string> knownTypes;
  std::string curAccess; // access section inside a class body (C++)
  std::vector<std::string> nsStack_; // enclosing named namespace path
  std::vector<std::string> nsNames_; // every namespace name seen in the file

  Parser(const std::vector<Token>& t, std::vector<Diag>& d) : toks(t), diags(d) {
    const char* java[] = {
      "int", "long", "short", "byte", "char", "boolean", "bool", "float",
      "double", "void", "String", "Integer", "Long", "Short", "Byte",
      "Character", "Float", "Double", "Boolean", "Object", "RuntimeException",
      "Exception", "Throwable", "Error", "AssertionError", "Number",
      "Math", "System", "StringBuilder", "List", "ArrayList", "LinkedList",
      "Map", "HashMap", "Set", "HashSet", "TreeMap", "TreeSet", "Collections",
      "Arrays", "Vector", "Queue", "Deque", "Stack", "Optional", "StringBuffer"
    };
    const char* cpp[] = {
      "std::string", "std::vector", "std::map", "std::unordered_map", "std::set",
      "std::unordered_set", "std::list", "std::pair", "std::unique_ptr",
      "std::shared_ptr", "std::weak_ptr", "std::cout", "std::cin", "std::cerr",
      "std::endl", "std::exception", "std::runtime_error", "std::out_of_range",
      "std::logic_error", "std::invalid_argument", "std::length_error",
      "std::to_string", "std::stoi", "std::stol", "std::stoll", "std::stod",
      "std::stof", "std::max", "std::min", "std::abs", "std::find", "std::begin",
      "std::end", "std::size_t", "std::ostream", "std::istream", "std::ifstream",
      "std::ofstream", "std::stringstream", "std::ostringstream", "size_t", "auto",
      "string", "wstring",  // with 'using namespace std;'
      // Windows typedefs commonly used in casts: (BYTE*)p, (DWORD)n, ...
      "HANDLE", "HWND", "HINSTANCE", "HMODULE", "HMENU", "HICON", "HCURSOR",
      "HBRUSH", "HDC", "HGDIOBJ", "HFONT", "HPEN", "HRGN", "HBITMAP", "HKEY",
      "HRESULT", "LRESULT", "BOOL", "BOOLEAN", "DWORD", "WORD", "BYTE", "INT",
      "UINT", "LONG", "ULONG", "SHORT", "USHORT", "LONGLONG", "ULONGLONG",
      "WPARAM", "LPARAM", "COLORREF", "SIZE_T", "DWORD_PTR", "UINT_PTR",
      "ULONG_PTR", "INT_PTR", "LONG_PTR", "WCHAR", "CHAR", "TCHAR", "FLOAT",
      "DOUBLE", "VOID", "LPVOID", "LPCVOID", "PVOID", "LPWSTR", "LPCWSTR",
      "LPSTR", "LPCSTR", "LPCTSTR", "LPTSTR", "BSTR", "GUID", "REFIID"
    };
    for (const char* s : java) knownTypes.insert(s);
    for (const char* s : cpp) knownTypes.insert(s);
  }

  // ---- token helpers ----
  const Token& cur() const {
    size_t i = pos;
    while (i < toks.size() && toks[i].kind == Tok::Eol) ++i;
    return toks[i];
  }
  const Token& curRaw() const { return toks[pos]; }
  bool atEnd() const { return cur().kind == Tok::End; }
  void bump() {
    while (pos < toks.size() && toks[pos].kind == Tok::Eol) ++pos;
    if (pos < toks.size() && toks[pos].kind != Tok::End) ++pos;
  }
  bool isOp(const char* op) const { return cur().kind == Tok::Op && cur().text == op; }
  bool isWord(const char* w) const { return cur().kind == Tok::Word && cur().text == w; }
  bool acceptOp(const char* op) { if (isOp(op)) { bump(); return true; } return false; }
  bool acceptWord(const char* w) { if (isWord(w)) { bump(); return true; } return false; }
  void expectOp(const char* op) {
    if (!acceptOp(op))
      throw ParseError(cur().line, "expected '" + std::string(op) + "' but got '" + cur().text + "'");
  }
  void expectWord(const char* w) {
    if (!acceptWord(w))
      throw ParseError(cur().line, "expected '" + std::string(w) + "' but got '" + cur().text + "'");
  }
  std::string expectIdent() {
    if (cur().kind != Tok::Word) {
      const Token& t = cur();
      throw ParseError(t.line, "expected identifier but got '" + t.text + "'");
    }
    std::string s = cur().text;
    bump();
    return s;
  }
  void warn(const std::string& m) {
    Diag d;
    d.line = cur().line;
    d.msg = m;
    d.warning = true;
    diags.push_back(d);
  }
  [[noreturn]] void err(const std::string& m) { throw ParseError(cur().line, m); }

  // negative lookahead: token text after skipping EOLs
  std::string lookaheadText(int k) const {
    size_t i = pos;
    int seen = 0;
    while (i < toks.size()) {
      if (toks[i].kind == Tok::Eol) { ++i; continue; }
      if (seen == k) return toks[i].text;
      ++seen;
      ++i;
    }
    return "";
  }

  // negative lookahead: k-th non-Eol token (nullptr past the end)
  const Token* peekTok(int k) const {
    size_t i = pos;
    int seen = 0;
    while (i < toks.size()) {
      if (toks[i].kind == Tok::Eol) { ++i; continue; }
      if (seen == k) return &toks[i];
      ++seen;
      ++i;
    }
    return nullptr;
  }

  // Calling-convention decorations (Win32/Win64 macros and MSVC extensions)
  // carry no Java meaning; skip them wherever a declarator name is expected:
  // 'LRESULT CALLBACK Foo(...)', 'void __stdcall bar(int)'.
  void skipCallSpecs() {
    while (cur().kind == Tok::Word) {
      const std::string w = cur().text;
      if (isConvWord(w)) {
        bump();
        if (w == "__declspec" && isOp("(")) skipBalancedParen();
        continue;
      }
      if (w == "__forceinline" || w == "__inline" || w == "__ptr32" ||
          w == "__ptr64" || w == "__unaligned") {
        bump();
        continue;
      }
      break;
    }
  }

  // An unknown ALL_CAPS token in front of a declaration is almost always an
  // export / visibility macro coming from a header we do not follow
  // ('PLUGIN_API', 'DLLEXPORT', 'API', ...). Without the #include the token is
  // undefined here and would otherwise make the whole declaration fail.
  bool looksLikeExportMacro(const std::string& w) const {
    if (w.size() < 2) return false;
    bool hasLetter = false;
    for (char c : w) {
      if (c >= 'a' && c <= 'z') return false;  // mixed case -> a real type name
      if (c >= 'A' && c <= 'Z') hasLetter = true;
      if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    if (!hasLetter) return false;
    if (knownTypes.count(w) || isModifierWord(w) || isConvWord(w)) return false;
    if (w == "NULL" || w == "TRUE" || w == "FALSE") return false;
    return true;
  }

  // Skips one leading export/visibility macro when what follows really looks
  // like a declarator. Returns true when a token was consumed.
  bool skipExportMacro() {
    if (cur().kind != Tok::Word) return false;
    std::string w = cur().text;
    if (!looksLikeExportMacro(w)) return false;
    const Token* t1 = peekTok(1);
    if (!t1 || t1->kind != Tok::Word) return false;
    bool declarator = knownTypes.count(t1->text) != 0;
    if (!declarator) {
      const Token* t2 = peekTok(2);
      if (!t2) return false;
      if (t2->kind == Tok::Op && (t2->text == "*" || t2->text == "&")) {
        declarator = true;  // 'MACRO Foo* x' / 'MACRO Foo& x'
      } else if (t2->kind == Tok::Word) {
        const Token* t3 = peekTok(3);
        // 'MACRO Foo bar(' -> function, 'MACRO Foo bar;' / '= ...' -> variable
        if (t3 && t3->kind == Tok::Op &&
            (t3->text == "(" || t3->text == ";" || t3->text == "=" ||
             t3->text == "[" || t3->text == ",")) {
          declarator = true;
        }
      }
    }
    if (!declarator) return false;
    Diag d;
    d.line = cur().line;
    d.msg = "unknown macro '" + w + "' treated as an export specifier and skipped";
    d.warning = true;
    diags.push_back(d);
    bump();
    return true;
  }

  // 'extern "C" { ... }' linkage block or 'extern "C" decl'. Consumes the
  // spec and returns true when fully handled; for the declaration form the
  // declaration itself is parsed here so the caller can just continue.
  bool tryLinkageSpec() {
    if (!isWord("extern")) return false;
    const Token* t1 = peekTok(1);
    if (!t1 || t1->kind != Tok::Str) return false;
    bump();  // 'extern'
    bump();  // "C" / "C++"
    if (isOp("{")) {
      bump();
      parseNamespaceItems();
      expectOp("}");
      acceptOp(";");
      return true;
    }
    parseFreeFunction({}, {});
    return true;
  }

  // ---- directive ----
  void parseDirective() {
    int ln = cur().line;
    bump(); // '#'
    std::vector<std::string> parts;
    while (curRaw().kind != Tok::Eol && curRaw().kind != Tok::End) {
      parts.push_back(curRaw().text);
      bump();
    }
    if (parts.empty()) return;
    if (parts[0] == "include") {
      std::string target;
      if (parts.size() >= 2) {
        if (parts[1] == "<") {
          target = "<";
          for (size_t i = 2; i < parts.size(); ++i) target += parts[i];
        } else {
          target = "\"" + parts[1] + "\"";
        }
      }
      prog.includes.push_back("#include " + target);
    } else {
      Diag d;
      d.line = ln;
      d.msg = "preprocessor directive '" + parts[0] + "' ignored";
      d.warning = true;
      diags.push_back(d);
    }
  }

  // ---- package / import / using ----
  void parsePackage() {
    bump(); // 'package'
    std::vector<std::string> parts;
    parts.push_back(expectIdent());
    while (acceptOp(".")) parts.push_back(expectIdent());
    expectOp(";");
    if (!prog.packageParts.empty()) {
      Diag d;
      d.line = cur().line;
      d.msg = "multiple package/namespace declarations; keeping the first";
      d.warning = true;
      diags.push_back(d);
    } else {
      prog.packageParts = parts;
    }
  }

  void parseImport() {
    bump(); // 'import'
    std::string s = expectIdent();
    while (acceptOp(".")) {
      if (isOp("*")) { s += ".*"; bump(); break; }
      s += "." + expectIdent();
    }
    expectOp(";");
    prog.imports.push_back(s);
  }

  void parseUsing() {
    bump(); // 'using'
    if (acceptWord("namespace")) {
      std::string ns = expectIdent();
      while (acceptOp("::")) ns += "::" + expectIdent();
      expectOp(";");
      prog.usingNs.push_back(ns);
    } else {
      std::string s = expectIdent();
      while (acceptOp("::")) s += "::" + expectIdent();
      if (acceptOp("=")) {
        // type alias: 'using X = Y;' -> no Java equivalent
        while (!acceptOp(";") && !atEnd()) bump();
        warn("type alias '" + s + "' ignored");
        return;
      }
      expectOp(";");
      (void)s;
    }
  }

  // ---- types ----
  TypePtr parseType() {
    auto T = std::make_shared<Type>();
    // C++ elaborated type specifier: 'struct tm t;', 'enum Color c;'.
    // Only when it is not a definition ('struct X {'), which is handled
    // elsewhere; here the keyword is a no-op qualifier.
    if (cur().kind == Tok::Word &&
        (cur().text == "struct" || cur().text == "class" ||
         cur().text == "union" || cur().text == "enum") &&
        lookaheadText(1) != "{" && lookaheadText(2) != "{") {
      bump();
    }
    bool loop = true;
    bool anyQual = false;
    while (loop) {
      loop = false;
      if (acceptWord("const")) { T->isConst = true; loop = true; anyQual = true; }
      else if (acceptWord("unsigned")) { T->isUnsigned = true; loop = true; anyQual = true; }
      else if (acceptWord("volatile")) { T->isVolatile = true; loop = true; anyQual = true; }
    }
    // Bare specifier with no type name: 'unsigned' / 'signed' mean 'int'
    // ('(unsigned)x'), and this is never a valid start of an expression.
    if (cur().kind != Tok::Word) {
      if (anyQual) { T->base = "int"; return T; }
      T->base = expectIdent();  // reports "expected identifier" as before
      return T;
    }
    T->base = expectIdent();
    // normalize composite integer spellings
    if (T->base == "unsigned" && cur().kind == Tok::Word) {
      T->base = cur().text;
      bump();
      if (T->base == "long" && acceptWord("long")) T->base = "long long";
      else if (acceptWord("int")) { /* keep */ }
    } else if (T->base == "long") {
      if (acceptWord("long")) T->base = "long long";
      else if (acceptWord("int")) { /* keep long */ }
    } else if (T->base == "short") {
      if (acceptWord("int")) { /* keep short */ }
    } else if (T->base == "signed") {
      if (cur().kind == Tok::Word) { T->base = cur().text; bump(); }
      else T->base = "int";
      if (acceptWord("int")) { /* keep */ }
    }
    // dotted / scoped names
    while (isOp(".") || isOp("::")) {
      std::string sep = isOp(".") ? "." : "::";
      bump();
      std::string part = expectIdent();
      T->base += sep + part;
    }
    // generic / template arguments (diamond '<>' allowed)
    if (isOp("<")) {
      size_t argSave = pos;
      bump();
      bool ok = acceptOp(">");
      if (!ok) {
        ok = true;
        do {
          try {
            T->args.push_back(*parseType());
          } catch (ParseError&) {
            ok = false;
            break;
          }
        } while (acceptOp(","));
        if (ok && !acceptOp(">")) ok = false;
      }
      if (!ok) {
        // Unsupported template argument (a function type such as
        // 'std::function<void(int)>', a non-type argument, ...): keep the
        // base name, drop the arguments and skip to the matching '>' so the
        // declarator parses normally.
        pos = argSave;  // at '<'
        int depth = 0;
        while (!atEnd()) {
          if (isOp("<")) { ++depth; bump(); }
          else if (isOp(">")) { --depth; bump(); if (depth <= 0) break; }
          else if (isOp("(")) { skipBalancedParen(); }
          else bump();
        }
        T->args.clear();
      }
    }
    // array suffixes: Java '[]' or C++ '[expr]'; pointer/reference suffixes.
    // Process them interleaved so C++ forms like 'char* argv[]' parse cleanly.
    // cv-qualifiers after '*'/'&' ('char const* const p') are skipped.
    while (isOp("[") || isOp("*") || isOp("&") || isWord("const") ||
           isWord("volatile")) {
      if (isWord("const") || isWord("volatile")) {
        bump();  // cv-qualifier on the pointer itself: no Java meaning
        continue;
      }
      if (isOp("[")) {
        bump();
        if (acceptOp("]")) {
          T->dims++;
        } else {
          T->dims++;
          if (!T->arrSize) T->arrSize = parseExpr();
          else { warn("multi-dimensional array sizes: keeping the first"); parseExpr(); }
          expectOp("]");
        }
      } else if (isOp("*")) {
        T->isPtr = true;
        bump();
      } else {
        T->isRef = true;
        bump();
      }
    }
    return T;
  }

  // Java-style type parameters after class name: <T, U extends Number>
  std::vector<std::string> parseTypeParams() {
    std::vector<std::string> out;
    if (!isOp("<")) return out;
    bump();
    do {
      std::string n = expectIdent();
      out.push_back(n);
      if (acceptWord("extends")) parseType();
      else if (acceptWord("super")) parseType();
    } while (acceptOp(","));
    expectOp(">");
    return out;
  }

  // C++ template prefix: template<typename T, class U>
  std::vector<std::string> parseTemplatePrefix() {
    std::vector<std::string> out;
    if (!acceptWord("template")) return out;
    expectOp("<");
    do {
      acceptWord("typename");
      acceptWord("class");
      std::string n = expectIdent();
      out.push_back(n);
      if (acceptOp("=")) parseExpr();      // default template argument
    } while (acceptOp(","));
    expectOp(">");
    return out;
  }

  // ---- modifiers ----
  std::vector<std::string> parseModifiers() {
    std::vector<std::string> out;
    while (cur().kind == Tok::Word) {
      const std::string w = cur().text;
      if (isModifierWord(w)) {
        out.push_back(w);
        bump();
        continue;
      }
      if (isConvWord(w)) {
        bump();
        if (w == "__declspec" && isOp("(")) skipBalancedParen();
        continue;
      }
      if (w == "__forceinline" || w == "__inline" || w == "__ptr32" ||
          w == "__ptr64" || w == "__unaligned") {
        bump();
        continue;
      }
      break;
    }
    return out;
  }

  void skipBalancedParen() {
    int depth = 0;
    while (!atEnd()) {
      if (isOp("(")) { ++depth; bump(); }
      else if (isOp(")")) { --depth; bump(); if (depth <= 0) break; }
      else bump();
    }
  }

  // Java annotations: skip '@Name' and '@Name(args)' entirely.
  void skipAnnotations() {
    while (isOp("@")) {
      bump();
      if (cur().kind == Tok::Word) bump();
      if (isOp("(")) skipBalancedParen();
    }
  }

  // ---- expressions (precedence climbing) ----
  ExprPtr parseExpr() { return parseAssign(); }

  static bool isAssignOp(const std::string& t) {
    return t == "=" || t == "+=" || t == "-=" || t == "*=" || t == "/=" ||
           t == "%=" || t == "&=" || t == "|=" || t == "^=";
  }

  ExprPtr parseAssign() {
    ExprPtr l = parseCond();
    if (cur().kind == Tok::Op && isAssignOp(cur().text)) {
      std::string op = cur().text;
      int ln = cur().line;
      bump();
      auto e = std::make_shared<Expr>();
      e->k = Expr::K::Assign;
      e->op = op;
      e->lhs = l;
      e->rhs = parseAssign();
      e->line = ln;
      return e;
    }
    return l;
  }

  ExprPtr parseCond() {
    ExprPtr c = parseOr();
    if (isOp("?")) {
      int ln = cur().line;
      bump();
      auto e = std::make_shared<Expr>();
      e->k = Expr::K::Ternary;
      e->lhs = c;
      e->rhs = parseAssign();
      expectOp(":");
      e->args.push_back(parseCond());
      e->line = ln;
      return e;
    }
    return c;
  }

  ExprPtr parseBin(const std::function<ExprPtr()>& next, const std::set<std::string>& ops) {
    ExprPtr l = next();
    while (cur().kind == Tok::Op && ops.count(cur().text)) {
      std::string op = cur().text;
      int ln = cur().line;
      bump();
      auto e = std::make_shared<Expr>();
      e->k = Expr::K::Binary;
      e->op = op;
      e->lhs = l;
      e->rhs = next();
      e->line = ln;
      l = e;
    }
    return l;
  }

  ExprPtr parseOr() {
    return parseBin([this]() { return parseAnd(); }, {"||"});
  }
  ExprPtr parseAnd() {
    return parseBin([this]() { return parseBitOr(); }, {"&&"});
  }
  ExprPtr parseBitOr() {
    return parseBin([this]() { return parseBitXor(); }, {"|"});
  }
  ExprPtr parseBitXor() {
    return parseBin([this]() { return parseBitAnd(); }, {"^"});
  }
  ExprPtr parseBitAnd() {
    return parseBin([this]() { return parseEquality(); }, {"&"});
  }
  ExprPtr parseEquality() {
    return parseBin([this]() { return parseRelational(); }, {"==", "!="});
  }
  ExprPtr parseRelational() {
    return parseBin([this]() { return parseShift(); }, {"<", "<=", ">", ">="});
  }
  // Shift operators. The lexer splits '>>' into two '>' tokens to keep nested
  // generics ('vector<vector<int>>') working, so at shift level a '>'
  // immediately followed by another '>' is a right shift; '<<=', '>>=' and
  // the plain shifts are handled here (shift binds tighter than relational).
  ExprPtr parseShift() {
    ExprPtr l = parseAdditive();
    while (cur().kind == Tok::Op) {
      const std::string t = cur().text;
      const bool isLeftShift = (t == "<<");
      // '>>=' may also arrive as '>' + '>=' when the lexer matches '>='
      // greedily after the first '>' of a shift operator.
      const bool isRightShiftAssign = (t == ">" && lookaheadText(1) == ">=");
      const bool isRightShift =
          isRightShiftAssign || (t == ">" && lookaheadText(1) == ">");
      if (!isLeftShift && !isRightShift) break;
      const std::string op = isLeftShift ? "<<" : ">>";
      int ln = cur().line;
      bump();
      if (isRightShiftAssign) {  // '>=' supplies '>' + '='
        bump();
        auto e = std::make_shared<Expr>();
        e->k = Expr::K::Assign;
        e->op = ">>=";
        e->lhs = l;
        e->rhs = parseAssign();
        e->line = ln;
        l = e;
        continue;
      }
      if (isRightShift) bump();  // second '>' of the split token
      if (isOp("=")) {           // '<<=' / '>>='
        bump();
        auto e = std::make_shared<Expr>();
        e->k = Expr::K::Assign;
        e->op = op + "=";
        e->lhs = l;
        e->rhs = parseAssign();
        e->line = ln;
        l = e;
        continue;
      }
      auto e = std::make_shared<Expr>();
      e->k = Expr::K::Binary;
      e->op = op;
      e->lhs = l;
      e->rhs = parseAdditive();
      e->line = ln;
      l = e;
    }
    return l;
  }
  ExprPtr parseAdditive() {
    return parseBin([this]() { return parseMult(); }, {"+", "-"});
  }
  ExprPtr parseMult() {
    return parseBin([this]() { return parseUnary(); }, {"*", "/", "%"});
  }

  ExprPtr parseUnary() {
    if (cur().kind == Tok::Op) {
      const std::string t = cur().text;
      if (t == "!" || t == "-" || t == "+" || t == "~" || t == "&" || t == "*") {
        int ln = cur().line;
        bump();
        auto e = std::make_shared<Expr>();
        e->k = Expr::K::Unary;
        e->op = t;
        e->lhs = parseUnary();
        e->line = ln;
        return e;
      }
      if (t == "++" || t == "--") {
        int ln = cur().line;
        bump();
        auto e = std::make_shared<Expr>();
        e->k = Expr::K::IncDec;
        e->op = t;
        e->isPrefix = true;
        e->lhs = parseUnary();
        e->line = ln;
        return e;
      }
      // NOTE: '(' must fall through to parsePostfix() (via parsePrimary)
      // so that postfix suffixes attach: '(a).b', '(a + b)[0]'.
    }
    return parsePostfix();
  }

  // True when the parenthesised group starting at index 'open' ('(') holds a
  // top-level comma, i.e. it is an argument list rather than a single
  // parenthesised expression.
  bool parenGroupHasTopComma(size_t open) {
    int depth = 0;
    for (size_t i = open; i < toks.size(); ++i) {
      if (toks[i].kind == Tok::Eol) continue;
      if (toks[i].kind == Tok::Op && toks[i].text == "(") { ++depth; continue; }
      if (toks[i].kind == Tok::Op && toks[i].text == ")") {
        if (depth == 0) continue;
        --depth;
        if (depth == 0) return false;
        continue;
      }
      if (toks[i].kind == Tok::Op && toks[i].text == "," && depth == 1)
        return true;
    }
    return false;
  }

  ExprPtr parseCastOrParen() {
    size_t save = pos;
    int ln = cur().line;
    bump(); // '('
    bool castable = false;
    TypePtr T;
    try {
      T = parseType();
      if (cur().kind == Tok::Op && cur().text == ")") {
        // '(std::min)(a, b)' is a call (the idiom that shields the name from
        // the Win32 min/max macros), not a cast of '(a, b)': a parenthesised
        // cast operand never has a top-level comma.
        bool callLike = false;
        {
          size_t a = pos + 1;  // first token after ')'
          while (a < toks.size() && toks[a].kind == Tok::Eol) ++a;
          if (a < toks.size() && toks[a].kind == Tok::Op && toks[a].text == "(")
            callLike = parenGroupHasTopComma(a);
        }
        if (!callLike) {
        // A pointer/reference declarator suffix can only belong to a type,
        // never to a parenthesised expression: '(uint8_t*)&x' is a cast even
        // when 'uint8_t' is an unknown Win32 typedef.
        if (knownTypes.count(T->base) || T->isPtr || T->isRef) {
          castable = true;
        } else {
          // Unknown type name (a Win32 typedef, a class defined later...).
          // Treat '(T)' as a cast when the token after ')' can only start an
          // operand; '(a) * b' / '(a) - b' stay parenthesised expressions.
          size_t j = pos;
          while (j < toks.size() && toks[j].kind == Tok::Eol) ++j;
          if (j + 1 < toks.size()) {
            const Token& nx = toks[j + 1];
            // NOTE: '(' is deliberately absent: '(std::min)(a, b)' is a
            // function call (the idiom that shields the name from the Win32
            // min/max macros), not a cast of '(a, b)'.
            if (nx.kind == Tok::Word || nx.kind == Tok::Int ||
                nx.kind == Tok::Float || nx.kind == Tok::Str ||
                nx.kind == Tok::Char || nx.kind == Tok::Bool ||
                nx.kind == Tok::Null ||
                (nx.kind == Tok::Op && (nx.text == "{" || nx.text == "!" ||
                                        nx.text == "~")))
              castable = true;
          }
        }
        }
      }
    } catch (ParseError&) {
      castable = false;
    }
    if (castable && cur().kind == Tok::Op && cur().text == ")") {
      bump();
      auto e = std::make_shared<Expr>();
      e->k = Expr::K::Cast;
      e->type = T;
      e->lhs = parseUnary();
      e->line = ln;
      return e;
    }
    pos = save;
    bump(); // '('
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::Paren;
    e->lhs = parseAssign();
    e->line = ln;
    expectOp(")");
    return e;
  }

  ExprPtr parsePostfix() {
    ExprPtr e = parsePrimary();
    while (true) {
      if (cur().kind == Tok::Op && (cur().text == "." || cur().text == "->" || cur().text == "::")) {
        std::string sep = cur().text;
        int ln = cur().line;
        bump();
        std::string nm = expectIdent();
        auto n = std::make_shared<Expr>();
        n->k = (sep == ".") ? Expr::K::Member : (sep == "->") ? Expr::K::Arrow : Expr::K::Scope;
        n->obj = e;
        n->name = nm;
        n->line = ln;
        e = n;
      } else if (isOp("(")) {
        int ln = cur().line;
        bump();
        auto c = std::make_shared<Expr>();
        c->k = Expr::K::Call;
        c->callee = e;
        c->line = ln;
        if (!acceptOp(")")) {
          c->args.push_back(parseExpr());
          while (acceptOp(",")) c->args.push_back(parseExpr());
          expectOp(")");
        }
        e = c;
      } else if (isOp("[")) {
        int ln = cur().line;
        bump();
        auto ix = std::make_shared<Expr>();
        ix->k = Expr::K::Index;
        ix->obj = e;
        ix->index = parseExpr();
        expectOp("]");
        ix->line = ln;
        e = ix;
      } else if (isOp("<")) {
        // Template-id in call position: explicit template arguments
        // ('std::sort<int>(...)') or a functional cast with template args
        // ('istreambuf_iterator<char>(inFile)'). Speculatively parse
        // '<' type-args '>' and require a '(' right after; otherwise restore
        // and let the relational layer handle '<'.
        size_t save = pos;
        bool ok = true;
        try {
          bump();  // '<'
          if (!acceptOp(">")) {
            do { (void)parseType(); } while (acceptOp(","));
            expectOp(">");
          }
        } catch (const ParseError&) {
          ok = false;
        }
        if (ok && isOp("(")) {
          int ln = cur().line;
          bump();
          auto c = std::make_shared<Expr>();
          c->k = Expr::K::Call;
          c->callee = e;
          c->line = ln;
          if (!acceptOp(")")) {
            c->args.push_back(parseExpr());
            while (acceptOp(",")) c->args.push_back(parseExpr());
            expectOp(")");
          }
          e = c;
        } else {
          pos = save;
          break;  // '<' belongs to a relational expression
        }
      } else if (cur().kind == Tok::Op && (cur().text == "++" || cur().text == "--")) {
        std::string op = cur().text;
        int ln = cur().line;
        bump();
        auto p = std::make_shared<Expr>();
        p->k = Expr::K::IncDec;
        p->op = op;
        p->isPrefix = false;
        p->lhs = e;
        p->line = ln;
        e = p;
      } else {
        break;
      }
    }
    return e;
  }

  ExprPtr parseNew() {
    int ln = cur().line;
    auto e = std::make_shared<Expr>();
    e->line = ln;
    TypePtr T = parseType();
    if (isOp("(")) {
      bump();
      e->k = Expr::K::NewObj;
      e->type = T;
      if (!acceptOp(")")) {
        e->args.push_back(parseExpr());
        while (acceptOp(",")) e->args.push_back(parseExpr());
        expectOp(")");
      }
      return e;
    }
    if (isOp("[")) {
      e->k = Expr::K::NewArr;
      e->type = T;
      while (isOp("[")) {
        bump();
        if (acceptOp("]")) {
          T->dims++;
        } else {
          T->dims++;
          if (!e->sizeEx) e->sizeEx = parseExpr();
          else {
            warn("multi-dimensional array sizes: using the first");
            parseExpr();
          }
          expectOp("]");
        }
      }
      if (isOp("{")) {
        bump();
        if (!acceptOp("}")) {
          do { e->args.push_back(parseExpr()); } while (acceptOp(","));
          expectOp("}");
        }
      }
      return e;
    }
    // C++-style 'new Foo' without parentheses
    e->k = Expr::K::NewObj;
    e->type = T;
    return e;
  }

  // Lambda expression: '[capture](params) [mutable] [-> ret] { body }'.
  // Captures have no Java equivalent and are recorded for diagnostics only;
  // the body maps to a Java lambda block.
  ExprPtr parseLambda() {
    int ln = cur().line;
    bump();  // '['
    std::string capture;
    int depth = 0;
    while (!atEnd()) {
      if (isOp("[") || isOp("(") || isOp("{")) ++depth;
      else if (isOp("]") || isOp(")") || isOp("}")) {
        if (depth == 0 && isOp("]")) break;  // end of the capture list
        --depth;
      }
      if (!capture.empty()) capture += " ";
      capture += cur().text;
      bump();
    }
    expectOp("]");
    // qualifiers before the parameter list (rare but legal)
    while (isWord("mutable") || isWord("constexpr") || isWord("consteval")) bump();
    std::vector<LambdaP> params;
    if (isOp("(")) {
      bump();
      auto tmp = std::make_shared<Method>();
      parseParams(tmp);
      expectOp(")");
      for (auto& p : tmp->params)
        params.push_back({p.type, p.name, p.varargs});
    }
    std::string retTxt;
    if (isOp("->")) {
      bump();
      TypePtr rt = parseType();
      retTxt = rt->str();
      skipCallSpecs();
    }
    // qualifiers after the parameter list
    while (isWord("mutable") || isWord("constexpr") || isWord("consteval")) bump();
    if (acceptWord("noexcept") && isOp("(")) skipBalancedParen();
    if (!isOp("{")) err("expected '{' to open lambda body");
    auto body = parseBlock();
    auto e = std::make_shared<Expr>();
    e->k = Expr::K::Lambda;
    e->line = ln;
    e->lparams = params;
    e->lbody = body;
    e->lret = retTxt;
    e->lcapture = capture;
    return e;
  }

  ExprPtr parsePrimary() {
    auto e = std::make_shared<Expr>();
    e->line = cur().line;
    const Tok k = cur().kind;
    if (k == Tok::Int) { e->k = Expr::K::IntLit; e->sval = cur().text; bump(); return e; }
    if (k == Tok::Float) { e->k = Expr::K::FloatLit; e->sval = cur().text; bump(); return e; }
    if (k == Tok::Str) {
      // C++ concatenates adjacent string literals ('L"a" L"b"'); Java uses
      // '+', so the payloads are merged into a single literal here.
      e->k = Expr::K::StrLit;
      e->sval = cur().text;
      bump();
      while (cur().kind == Tok::Str) {
        e->sval += cur().text;
        bump();
      }
      return e;
    }
    if (k == Tok::Char) { e->k = Expr::K::CharLit; e->sval = cur().text; bump(); return e; }
    if (k == Tok::Bool) { e->k = Expr::K::BoolLit; e->bval = (cur().text == "true"); bump(); return e; }
    if (k == Tok::Null) { e->k = Expr::K::Null; bump(); return e; }
    if (k == Tok::Word) {
      if (cur().text == "this") { e->k = Expr::K::This; bump(); return e; }
      if (cur().text == "new") { bump(); return parseNew(); }
      // C++ named casts: static_cast<T>(x) & friends.
      if ((cur().text == "static_cast" || cur().text == "dynamic_cast" ||
           cur().text == "reinterpret_cast" || cur().text == "const_cast") &&
          lookaheadText(1) == "<") {
        bump();  // cast keyword
        bump();  // '<'
        e->k = Expr::K::Cast;
        e->type = parseType();
        expectOp(">");
        expectOp("(");
        e->lhs = parseExpr();
        expectOp(")");
        return e;
      }
      e->k = Expr::K::Name;
      e->name = cur().text;
      bump();
      return e;
    }
    if (k == Tok::Op && cur().text == "(") return parseCastOrParen();
    // C++ global-scope qualification: '::isspace', 'std::tolower' reached as
    // '::std::tolower'. Java has no '::', so the qualifier is dropped.
    if (k == Tok::Op && cur().text == "::") {
      bump();
      std::string qn;
      while (cur().kind == Tok::Word) {
        if (!qn.empty()) qn += "::";
        qn += cur().text;
        bump();
        if (!acceptOp("::")) break;
      }
      e->k = Expr::K::Name;
      e->name = qn.empty() ? std::string("?") : qn;
      return e;
    }
    // Lambda expression: '[capture](params) [mutable] [-> T] { body }'
    if (k == Tok::Op && cur().text == "[") return parseLambda();
    if (k == Tok::Op && cur().text == "{") {
      bump();
      e->k = Expr::K::ArrayInit;
      if (!acceptOp("}")) {
        // A trailing comma is legal in C++ ('{ a, b, }') and common in
        // hand-written initializer tables.
        do {
          if (isOp("}")) break;
          e->args.push_back(parseExpr());
        } while (acceptOp(","));
        expectOp("}");
      }
      return e;
    }
    const Token& t = cur();
    throw ParseError(t.line, "unexpected token '" + t.text + "' in expression");
  }

  // ---- statements ----
  BlockPtr parseBlock() {
    auto B = std::make_shared<Block>();
    expectOp("{");
    while (!isOp("}") && !atEnd()) {
      try {
        B->stmts.push_back(parseStmt());
      } catch (const ParseError& e) {
        // Skip the offending statement and keep translating the rest.
        Diag d;
        d.line = e.line;
        d.msg = std::string("statement skipped: ") + e.what();
        d.warning = true;
        diags.push_back(d);
        recoverStmt();
      }
    }
    expectOp("}");
    return B;
  }

  void skipBalanced() {
    int depth = 0;
    while (!atEnd()) {
      if (isOp("{")) { ++depth; bump(); }
      else if (isOp("}")) { --depth; bump(); if (depth <= 0) break; }
      else bump();
    }
  }

  void skipNestedType() {
    while (!atEnd() && !isOp("{") && !isOp(";")) bump();
    if (isOp("{")) skipBalanced();
    else acceptOp(";");
  }

  bool skipUntilSemi() {
    while (!atEnd() && !isOp(";")) bump();
    return acceptOp(";");
  }

  // Error recovery inside a class body: skip to the next ';' at depth 0 or to
  // the closing '}' of the class, so one malformed member does not kill the
  // whole translation unit.
  void recoverMember() {
    int depth = 0;
    while (!atEnd()) {
      if (isOp("{")) { ++depth; bump(); }
      else if (isOp("}")) { if (depth == 0) return; --depth; bump(); }
      else if (isOp(";") && depth == 0) { bump(); return; }
      else bump();
    }
  }

  // Error recovery inside a block: same idea, one nesting level deeper.
  void recoverStmt() {
    int depth = 0;
    while (!atEnd()) {
      if (isOp("{")) { ++depth; bump(); }
      else if (isOp("}")) { if (depth == 0) return; --depth; bump(); }
      else if (isOp(";") && depth == 0) { bump(); return; }
      else bump();
    }
  }

  StmtPtr parseStmt() {
    auto S = std::make_shared<Stmt>();
    S->line = cur().line;
    if (isOp("{")) { S->k = Stmt::K::Block; S->block = parseBlock(); return S; }
    if (acceptOp(";")) { S->k = Stmt::K::Empty; return S; }
    if (isWord("if")) {
      bump();
      S->k = Stmt::K::If;
      expectOp("(");
      S->cond = parseExpr();
      expectOp(")");
      S->thenS = parseStmt();
      if (acceptWord("else")) S->elseS = parseStmt();
      return S;
    }
    if (isWord("while")) {
      bump();
      S->k = Stmt::K::While;
      expectOp("(");
      S->cond = parseExpr();
      expectOp(")");
      S->body = parseStmt();
      return S;
    }
    if (isWord("do")) {
      bump();
      S->k = Stmt::K::Do;
      S->body = parseStmt();
      expectWord("while");
      expectOp("(");
      S->cond = parseExpr();
      expectOp(")");
      expectOp(";");
      return S;
    }
    if (isWord("for")) { bump(); return parseFor(); }
    if (isWord("switch")) {
      bump();
      S->k = Stmt::K::Switch;
      expectOp("(");
      S->swCond = parseExpr();
      expectOp(")");
      expectOp("{");
      while (!isOp("}") && !atEnd()) {
        SwitchCase sc;
        if (acceptWord("case")) { sc.value = parseExpr(); expectOp(":"); }
        else if (acceptWord("default")) { sc.isDefault = true; expectOp(":"); }
        else err("expected 'case' or 'default' inside switch");
        while (!isOp("}") && !isWord("case") && !isWord("default") && !atEnd())
          sc.stmts.push_back(parseStmt());
        S->swCases.push_back(sc);
      }
      expectOp("}");
      return S;
    }
    if (isWord("return")) {
      bump();
      S->k = Stmt::K::Return;
      if (!acceptOp(";")) {
        S->expr = parseExpr();
        S->hasValue = true;
        expectOp(";");
      }
      return S;
    }
    if (isWord("throw")) {
      bump();
      S->k = Stmt::K::Throw;
      S->expr = parseExpr();
      expectOp(";");
      return S;
    }
    if (isWord("try")) {
      bump();
      S->k = Stmt::K::Try;
      S->tryBlock = parseBlock();
      while (acceptWord("catch")) {
        expectOp("(");
        CatchClause cc;
        if (acceptOp("...")) {
          // C++ catch-all: Java has no direct equivalent, so it becomes
          // 'catch (Throwable ...)' with a generated name.
          cc.type = std::make_shared<Type>();
          cc.type->base = "Throwable";
          cc.name = "__any";
          cc.isCatchAll = true;
        } else {
          cc.type = parseType();
          cc.name = (cur().kind == Tok::Word) ? expectIdent() : "";
        }
        expectOp(")");
        cc.body = parseBlock();
        S->catches.push_back(cc);
      }
      if (acceptWord("finally")) S->finallyBlock = parseBlock();
      return S;
    }
    if (isWord("break")) { bump(); S->k = Stmt::K::Break; expectOp(";"); return S; }
    if (isWord("continue")) { bump(); S->k = Stmt::K::Continue; expectOp(";"); return S; }
    // C++ local type definition: 'struct X { ... } [decl];', 'union U { ... };'
    // and 'enum [class] E [: T] { ... };'. Java has no equivalent (nested
    // classes cannot be declared inside a method body), so the whole
    // definition is skipped with a warning.
    if (isWord("struct") || isWord("class") || isWord("union") || isWord("enum")) {
      const std::string kw = cur().text;
      size_t j = pos + 1;
      if (kw == "enum" && j < toks.size() && toks[j].kind == Tok::Word &&
          toks[j].text == "class")
        ++j;  // 'enum class'
      if (j < toks.size() && toks[j].kind == Tok::Word) ++j;  // optional tag name
      bool isDef = false;
      while (j < toks.size()) {
        if (toks[j].kind == Tok::Op) {
          if (toks[j].text == "{") { isDef = true; break; }
          if (toks[j].text == ";" || toks[j].text == "(" || toks[j].text == "=") break;
        }
        ++j;
      }
      if (isDef) {
        pos = j;                 // at '{'
        skipBalanced();          // consume the member list
        while (!acceptOp(";") && !atEnd()) {
          if (isOp("{")) { skipBalanced(); continue; }
          if (isOp("}")) break;  // never swallow the enclosing block
          bump();
        }
        warn("local " + kw + " definition skipped (no Java equivalent)");
        S->k = Stmt::K::Empty;
        return S;
      }
    }
    // C++ label: Word ':' (single colon, not '::')
    if (cur().kind == Tok::Word && lookaheadText(1) == ":" && lookaheadText(2) != ":") {
      std::string lab = cur().text;
      bump();
      bump(); // ':'
      S->k = Stmt::K::Label;
      S->label = lab;
      S->labelBody = parseStmt();
      Diag d;
      d.line = S->line;
      d.msg = "goto label '" + lab + "' cannot be mapped to Java; kept as a block with the label";
      d.warning = true;
      diags.push_back(d);
      return S;
    }
    return parseVarDeclOrExprStmt(S);
  }

  bool forIsRange() {
    size_t i = pos; // at '('
    while (i < toks.size()) {
      const Tok k = toks[i].kind;
      if (k == Tok::Eol) { ++i; continue; }
      if (k == Tok::End) return false;
      if (k == Tok::Op) {
        if (toks[i].text == ";") return false;
        if (toks[i].text == ":") return true;
      }
      ++i;
    }
    return false;
  }

  StmtPtr parseFor() {
    auto S = std::make_shared<Stmt>();
    S->line = cur().line;
    expectOp("(");
    if (forIsRange()) {
      S->k = Stmt::K::ForEach;
      S->eachType = parseType();
      S->eachName = expectIdent();
      expectOp(":");
      S->eachColl = parseExpr();
      expectOp(")");
      S->body = parseStmt();
      return S;
    }
    S->k = Stmt::K::For;
    if (!acceptOp(";")) {
      size_t save = pos;
      bool isDecl = false;
      try {
        parseType();
        std::string nm = expectIdent();
        if (isOp("=") || isOp(",") || isOp(";")) isDecl = true;
        (void)nm;
      } catch (ParseError&) { isDecl = false; }
      pos = save;
      if (isDecl) {
        S->hasForInitV = true;
        S->forInitV = parseVarDeclNoSemi();
      } else {
        S->forInitE = parseExpr();
      }
      expectOp(";");
    }
    if (!acceptOp(";")) {
      S->forCond = parseExpr();
      expectOp(";");
    }
    if (!isOp(")")) S->forInc = parseExpr();
    expectOp(")");
    S->body = parseStmt();
    return S;
  }

  // C++ declarator suffixes shared by locals, fields and globals: array
  // dimensions ('int a[10]', 'char* argv[]') and brace initializers.
  void parseDeclSuffixes(TypePtr T, ExprPtr& init, bool& hasBraceInit) {
    while (isOp("[")) {
      bump();
      T->dims++;
      if (!acceptOp("]")) {
        if (!T->arrSize) T->arrSize = parseExpr();
        else parseExpr();
        expectOp("]");
      }
    }
    if (isOp("{")) {
      // C++11 direct-list-init: 'Foo x{1, 2}'
      bump();
      auto e = std::make_shared<Expr>();
      e->k = Expr::K::ArrayInit;
      e->line = cur().line;
      if (!acceptOp("}")) {
        do { e->args.push_back(parseExpr()); } while (acceptOp(","));
        expectOp("}");
      }
      init = e;
      hasBraceInit = true;
    }
  }

  VarDeclS parseVarDeclNoSemi() {
    VarDeclS v;
    parseModifiers();  // leading storage specifiers: 'static const char x[]'
    v.type = parseType();
    do {
      Declarator d;
      skipCallSpecs();
      d.name = expectIdent();
      int savedDims = v.type->dims;            // array suffixes belong to
      ExprPtr savedSize = v.type->arrSize;     // this declarator only
      bool braceInit = false;
      parseDeclSuffixes(v.type, d.init, braceInit);
      if (!braceInit) {
        if (acceptOp("=")) d.init = parseExpr();
        else if (isOp("(")) {
          bump();
          auto e = std::make_shared<Expr>();
          e->k = Expr::K::Paren;
          e->line = cur().line;
          if (!acceptOp(")")) {
            e->args.push_back(parseExpr());
            while (acceptOp(",")) e->args.push_back(parseExpr());
            expectOp(")");
          }
          d.init = e;
        }
      }
      v.decls.push_back(d);
      v.type->dims = savedDims;
      v.type->arrSize = savedSize;
    } while (acceptOp(","));
    return v;
  }

  VarDeclS parseVarDecl() {
    VarDeclS v = parseVarDeclNoSemi();
    expectOp(";");
    return v;
  }

  StmtPtr parseVarDeclOrExprStmt(StmtPtr S) {
    size_t save = pos;
    bool isDecl = false;
    try {
      parseModifiers();  // 'static const ...' locals
      parseType();
      std::string nm = expectIdent();
      if (isOp("=") || isOp(",") || isOp(";") || isOp("(") || isOp("[") ||
          isOp("{"))
        isDecl = true;
      (void)nm;
    } catch (ParseError&) { isDecl = false; }
    pos = save;
    if (isDecl) {
      S->k = Stmt::K::VarDecl;
      S->vdecl = parseVarDecl();
      return S;
    }
    S->k = Stmt::K::Expr;
    S->expr = parseExpr();
    expectOp(";");
    return S;
  }

  // ---- members ----
  void parseParams(std::shared_ptr<Method> M) {
    while (!isOp(")") && !atEnd()) {
      skipAnnotations();
      if (acceptWord("final")) { /* Java final param: recorded via type const? ignored */ }
      Param p;
      p.type = parseType();
      if (acceptOp("...")) p.varargs = true;
      skipCallSpecs();
      if (cur().kind == Tok::Word) p.name = expectIdent();
      // C++/Java postfix array: 'char* argv[]' or 'String args[]'
      while (isOp("[")) {
        bump();
        if (!acceptOp("]")) { parseExpr(); expectOp("]"); }
        p.type->dims++;
      }
      if (acceptOp("=")) p.def = parseExpr();
      M->params.push_back(p);
      if (!acceptOp(",")) break;
    }
  }

  void stripMethodTrailers(std::shared_ptr<Method> M) {
    while (true) {
      if (acceptWord("const")) { M->mods.push_back("const"); }
      else if (acceptWord("override")) { M->mods.push_back("override"); }
      else if (acceptWord("final")) { M->mods.push_back("final"); }
      else if (acceptWord("noexcept")) {
        M->mods.push_back("noexcept");
        if (isOp("(")) skipBalancedParen();  // noexcept(expr)
      }
      else if (acceptWord("mutable")) { /* meaningless after a signature */ }
      else if (acceptWord("throw")) {
        // dynamic exception specification: 'throw()' / 'throw(Type)'
        if (isOp("(")) skipBalancedParen();
        else { while (!isOp("{") && !isOp(";") && !atEnd()) bump(); }
      }
      else if (isOp("->")) {
        // trailing return type: 'auto f() -> int'
        bump();
        TypePtr rt = parseType();
        if (M->ret && (M->ret->base == "auto" ||
                       endsWith(M->ret->base, "::auto")))
          M->ret = rt;
      }
      else if (acceptOp("=")) {
        // C++ pure virtual '= 0', '= default', '= delete'.
        if (cur().kind == Tok::Int && cur().text == "0") {
          bump();
          M->mods.push_back("pure");
        } else if (cur().kind == Tok::Word &&
                   (cur().text == "default" || cur().text == "delete")) {
          std::string d = cur().text;
          bump();
          M->mods.push_back(d);  // default/delete: body-less by design
        } else break;  // unexpected '=' after declaration; stop trailer scan
      }
      else break;
    }
  }

  void parseCtorInitList(std::shared_ptr<Method> M) {
    bump(); // ':'
    do {
      std::string n = expectIdent();
      ExprPtr e;
      if (acceptOp("(")) {
        if (!acceptOp(")")) {
          e = parseExpr();
          while (acceptOp(",")) parseExpr(); // extra ctor arguments ignored
          expectOp(")");
        }
      } else if (isOp("{")) {
        bump();
        if (!acceptOp("}")) { e = parseExpr(); expectOp("}"); }
      }
      M->initList.push_back({n, e});
    } while (acceptOp(","));
  }

  bool isCtorStart(const std::shared_ptr<ClassDecl>& C) {
    if (cur().kind != Tok::Word || cur().text != C->name) return false;
    return lookaheadText(1) == "(";
  }

  // Same signature (name + parameter shape)? Used to collapse a declaration
  // and its out-of-line definition into one Java method.
  static bool sameSig(const Method& a, const Method& b) {
    if (a.name != b.name || a.isCtor != b.isCtor) return false;
    if (a.params.size() != b.params.size()) return false;
    for (size_t i = 0; i < a.params.size(); ++i) {
      if (!a.params[i].type || !b.params[i].type) return false;
      const Type& x = *a.params[i].type;
      const Type& y = *b.params[i].type;
      if (x.base != y.base || x.dims != y.dims || x.isPtr != y.isPtr ||
          x.isRef != y.isRef || x.args.size() != y.args.size())
        return false;
    }
    return true;
  }

  // Add a method to a class, collapsing declaration/definition duplicates
  // (the definition wins) instead of emitting two Java methods.
  void addClassMethod(const std::shared_ptr<ClassDecl>& C,
                      const std::shared_ptr<Method>& M) {
    for (auto& ex : C->methods) {
      if (sameSig(ex, *M)) {
        if (M->body && !ex.body) ex = *M;  // keep the definition
        return;
      }
    }
    C->methods.push_back(*M);
  }

  // 'STDMETHOD(name)' / 'STDMETHOD_(ret, name)' COM helper macros expand to
  // 'virtual HRESULT STDMETHODCALLTYPE name' / 'virtual ret ... name'.
  bool tryStdMethod(std::vector<std::string>& mods, TypePtr& ret,
                    std::string& name) {
    if (!isWord("STDMETHOD") && !isWord("STDMETHOD_")) return false;
    const bool withRet = isWord("STDMETHOD_");
    bump();
    expectOp("(");
    TypePtr T;
    std::string nm;
    if (withRet) {
      T = parseType();
      expectOp(",");
      nm = expectIdent();
    } else {
      T = std::make_shared<Type>();
      T->base = "HRESULT";
      nm = expectIdent();
    }
    expectOp(")");
    mods.push_back("virtual");
    ret = T;
    name = nm;
    return true;
  }

  void parseMember(const std::shared_ptr<ClassDecl>& C, std::vector<std::string> mods,
                   std::vector<std::string> typeParams, int ln) {
    TypePtr stdRet;
    std::string stdName;
    if (tryStdMethod(mods, stdRet, stdName)) {
      auto M = std::make_shared<Method>();
      M->line = ln;
      M->mods = mods;
      M->typeParams = typeParams;
      M->ret = stdRet;
      M->name = stdName;
      if (isOp("(")) {
        bump();
        parseParams(M);
        expectOp(")");
        stripMethodTrailers(M);
        if (isOp("{")) M->body = parseBlock();
        else { M->body = nullptr; acceptOp(";"); }
      } else {
        acceptOp(";");
      }
      addClassMethod(C, M);
      return;
    }
    if (isCtorStart(C)) {
      auto M = std::make_shared<Method>();
      M->line = ln;
      M->mods = mods;
      M->typeParams = typeParams;
      M->isCtor = true;
      M->name = C->name;
      bump(); // class name
      expectOp("(");
      parseParams(M);
      expectOp(")");
      stripMethodTrailers(M);
      if (isOp(":")) parseCtorInitList(M);
      if (isOp("{")) M->body = parseBlock();
      else acceptOp(";");
      addClassMethod(C, M);
      return;
    }
    skipExportMacro();  // PLUGIN_API / DLLEXPORT / ... from an unread header
    TypePtr T = parseType();
    skipCallSpecs();
    std::string name = expectIdent();
    if (name == "operator") {
      // operator overloads (operator(), operator==, ...) have no Java
      // equivalent; consume the member and drop it.
      Diag d;
      d.line = ln;
      d.msg = "operator overload not convertible; skipped";
      d.warning = true;
      diags.push_back(d);
      if (isOp("(")) { bump(); if (isOp(")")) bump(); }
      else if (isOp("[")) { bump(); if (isOp("]")) bump(); }
      else bump();  // any single operator token (==, <, +, new, delete, ...)
      if (isOp("(")) skipBalancedParen();
      if (isOp("{")) skipBalanced();
      else acceptOp(";");
      return;
    }
    if (isOp("(")) {
      auto M = std::make_shared<Method>();
      M->line = ln;
      M->mods = mods;
      M->typeParams = typeParams;
      M->ret = T;
      M->name = name;
      bump(); // '('
      parseParams(M);
      expectOp(")");
      if (acceptWord("throws")) {
        do { M->throws.push_back(parseType()); } while (acceptOp(","));
      }
      stripMethodTrailers(M);
      if (isOp("{")) M->body = parseBlock();
      else { M->body = nullptr; acceptOp(";"); }
      addClassMethod(C, M);
      return;
    }
    // field(s)
    Field F;
    F.mods = mods;
    F.type = T;
    F.name = name;
    F.line = ln;
    bool braceInit = false;
    parseDeclSuffixes(T, F.init, braceInit);
    if (!braceInit && acceptOp("=")) F.init = parseExpr();
    C->fields.push_back(F);
    while (acceptOp(",")) {
      Field F2;
      F2.mods = mods;
      F2.type = std::make_shared<Type>(*T);
      F2.type->dims = 0;
      F2.type->arrSize = nullptr;
      F2.name = expectIdent();
      F2.line = ln;
      bool bi = false;
      parseDeclSuffixes(F2.type, F2.init, bi);
      if (!bi && acceptOp("=")) F2.init = parseExpr();
      C->fields.push_back(F2);
    }
    expectOp(";");
  }

  void parseClassBody(const std::shared_ptr<ClassDecl>& C) {
    while (!isOp("}") && !atEnd()) {
      int ln = cur().line;
      skipAnnotations();
      if (acceptOp(";")) continue;
      // C++ access section markers: private: / public: / protected:
      if (cur().kind == Tok::Word &&
          (cur().text == "public" || cur().text == "private" || cur().text == "protected") &&
          lookaheadText(1) == ":" && lookaheadText(2) != ":") {
        curAccess = cur().text;
        bump();
        bump(); // ':'
        continue;
      }
      if (isWord("friend")) {
        Diag d;
        d.line = ln;
        d.msg = "friend declaration skipped";
        d.warning = true;
        diags.push_back(d);
        skipUntilSemi();
        continue;
      }
      if (isWord("static_assert")) {
        Diag d;
        d.line = ln;
        d.msg = "static_assert skipped";
        d.warning = true;
        diags.push_back(d);
        bump();
        if (isOp("(")) skipBalancedParen();
        acceptOp(";");
        continue;
      }
      std::vector<std::string> mods = parseModifiers();
      // merge C++ access section into member modifiers when not explicit
      if (!curAccess.empty()) {
        bool hasAcc = false;
        for (const std::string& m : mods)
          if (m == "public" || m == "private" || m == "protected") hasAcc = true;
        if (!hasAcc) mods.insert(mods.begin(), curAccess);
      }
      if (isOp("}")) break;
      if (isOp("~")) { // destructor
        auto M = std::make_shared<Method>();
        M->line = ln;
        M->mods = mods;
        M->isDtor = true;
        bump();
        std::string nm = expectIdent();
        M->name = "~" + nm;
        if (acceptOp("(")) { parseParams(M); expectOp(")"); }
        stripMethodTrailers(M);
        if (isOp("{")) M->body = parseBlock();
        else acceptOp(";");
        addClassMethod(C, M);
        continue;
      }
      bool hasStatic = false;
      for (const std::string& m : mods) if (m == "static") hasStatic = true;
      if (hasStatic && isOp("{")) {
        Diag d;
        d.line = ln;
        d.msg = "static initializer block skipped";
        d.warning = true;
        diags.push_back(d);
        skipBalanced();
        continue;
      }
      if (isWord("class") || isWord("struct") || isWord("interface") || isWord("enum")) {
        Diag d;
        d.line = ln;
        d.msg = "nested type declaration skipped";
        d.warning = true;
        diags.push_back(d);
        skipNestedType();
        continue;
      }
      try {
        if (isWord("template")) {
          auto tp = parseTemplatePrefix();
          auto m2 = parseModifiers();
          parseMember(C, m2, tp, ln);
          continue;
        }
        parseMember(C, mods, {}, ln);
      } catch (const ParseError& e) {
        // One malformed member must not abort the whole translation unit.
        Diag d;
        d.line = e.line;
        d.msg = std::string("class member skipped: ") + e.what();
        d.warning = true;
        diags.push_back(d);
        recoverMember();
      }
    }
  }

  std::shared_ptr<ClassDecl> parseClass(std::vector<std::string> mods,
                                        std::vector<std::string> typeParams) {
    auto C = std::make_shared<ClassDecl>();
    C->mods = mods;
    C->typeParams = typeParams;
    C->ns = nsStack_;
    C->line = cur().line;
    if (acceptWord("interface")) C->isInterface = true;
    else if (acceptWord("struct")) C->isStruct = true;
    else expectWord("class");
    C->name = expectIdent();
    // trailing class-virt-specifiers: 'class X final : public Y'
    while (cur().kind == Tok::Word &&
           (cur().text == "final" || cur().text == "sealed"))
      { C->mods.push_back(cur().text); bump(); }
    curAccess = C->isStruct ? "public" : "private";
    knownTypes.insert(C->name);
    if (isOp("<")) {
      auto tp = parseTypeParams();
      if (!C->typeParams.empty()) {
        Diag d;
        d.line = cur().line;
        d.msg = "conflicting template/generic declarations on class '" + C->name + "'";
        d.warning = true;
        diags.push_back(d);
      }
      C->typeParams = tp;
    }
    if (acceptWord("extends")) {
      BaseSpec b;
      b.type = parseType();
      b.isPublic = true;
      C->bases.push_back(b);
    }
    if (acceptWord("implements")) {
      do {
        BaseSpec b;
        b.type = parseType();
        b.isPublic = true;
        C->bases.push_back(b);
      } while (acceptOp(","));
    }
    if (isOp(":")) {
      bump();
      do {
        BaseSpec b;
        if (acceptWord("public")) b.isPublic = true;
        else if (acceptWord("private")) b.isPublic = false;
        else if (acceptWord("protected")) b.isPublic = false;
        if (acceptWord("virtual")) b.isVirtual = true;
        b.type = parseType();
        C->bases.push_back(b);
      } while (acceptOp(","));
    }
    if (isOp(";")) { bump(); return nullptr; } // forward declaration
    expectOp("{");
    parseClassBody(C);
    expectOp("}");
    acceptOp(";");
    return C;
  }

  std::shared_ptr<EnumDecl> parseEnum(const std::vector<std::string>& mods) {
    auto E = std::make_shared<EnumDecl>();
    E->mods = mods;
    E->line = cur().line;
    bump(); // 'enum'
    if (acceptWord("class")) E->scoped = true;
    else if (acceptWord("struct")) E->scoped = true;
    E->name = expectIdent();
    knownTypes.insert(E->name);
    // fixed underlying type: 'enum class E : uint8_t { ... }'
    if (acceptOp(":")) parseType();
    expectOp("{");
    if (!acceptOp("}")) {
      do {
        std::string v = expectIdent();
        if (acceptOp("=")) {
          v += "=";
          v += cur().text;
          bump();
        }
        E->values.push_back(v);
      } while (acceptOp(","));
      expectOp("}");
    }
    acceptOp(";");
    return E;
  }

  // ---- top level ----
  void parseNamespaceItems() {
    while (!isOp("}") && !atEnd()) {
      if (cur().kind == Tok::Eol) { bump(); continue; }
      skipAnnotations();
      if (isOp("#")) { parseDirective(); continue; }
      if (tryLinkageSpec()) continue;
      if (isWord("namespace")) { parseNamespace(); continue; }
      if (isWord("import")) { parseImport(); continue; }
      if (isWord("using")) { parseUsing(); continue; }
      try {
        if (isWord("template")) {
          auto tp = parseTemplatePrefix();
          auto mods = parseModifiers();
          if (isWord("class") || isWord("struct") || isWord("interface")) {
            auto c = parseClass(mods, tp);
            if (c) prog.classes.push_back(c);
          } else {
            parseFreeFunction(mods, tp);
          }
          continue;
        }
        auto mods = parseModifiers();
        if (isWord("class") || isWord("struct") || isWord("interface")) {
          auto c = parseClass(mods, {});
          if (c) prog.classes.push_back(c);
        } else if (isWord("enum")) {
          prog.enums.push_back(parseEnum(mods));
        } else {
          parseFreeFunction(mods, {});
        }
      } catch (const ParseError& e) {
        // One malformed top-level declaration must not abort the file.
        Diag d;
        d.line = e.line;
        d.msg = std::string("top-level declaration skipped: ") + e.what();
        d.warning = true;
        diags.push_back(d);
        if (isOp("{")) {
          skipBalanced();
          continue;
        }
        if (!skipUntilSemi() && !isOp("}") && !atEnd()) bump();
      }
    }
  }

  void parseNamespace() {
    bump(); // 'namespace'
    if (cur().kind != Tok::Word) err("expected namespace name");
    std::vector<std::string> parts;
    parts.push_back(expectIdent());
    while (acceptOp("::")) parts.push_back(expectIdent());
    if (acceptOp("=")) {
      while (!acceptOp(";") && !atEnd()) bump();
      warn("namespace alias ignored");
      return;
    }
    if (!isOp("{")) err("expected '{' after namespace name");
    bump();
    // A C++ namespace is NOT a Java package: the whole translation goes into
    // one 'Main.java', and emitting 'package x;' there would put every class
    // in a package that the runner cannot address. Namespaces become holder
    // classes instead (see NamespaceItems).
    for (const std::string& p : parts)
      if (std::find(nsNames_.begin(), nsNames_.end(), p) == nsNames_.end())
        nsNames_.push_back(p);
    nsStack_.insert(nsStack_.end(), parts.begin(), parts.end());
    parseNamespaceItems();
    nsStack_.resize(nsStack_.size() - parts.size());
    expectOp("}");
  }

  // Looks like 'Ret? qualified :: name (' at current position? Returns true and
  // attaches the method when the pattern matches (simple return types only).
  // Out-of-line member definition: 'Ret Class::method(...) { ... }',
  // 'Class::Class(...) : ... { }' and 'Class::~Class() { }'. Returns false
  // when the declaration is not of this shape (the caller then treats it as
  // a free function or a global variable).
  bool tryQualifiedMethod(int ln, const std::vector<std::string>& mods) {
    size_t i = pos;
    while (i < toks.size() && toks[i].kind == Tok::Eol) ++i;
    if (i >= toks.size() || toks[i].kind != Tok::Word) return false;

    // Scan the declarator up to the '(' of the parameter list, collecting
    // identifiers and '::'/'.' separators in source order. A '~' marks a
    // destructor name.
    std::vector<std::string> units;  // words and separators, alternating
    bool wantWord = true;
    bool tilde = false;
    size_t j = i;
    for (; j < toks.size(); ++j) {
      const Token& t = toks[j];
      if (t.kind == Tok::Eol) continue;
      // Consecutive words are allowed: a multi-word return type such as
      // 'unsigned int Foo::f()' is scanned as several leading units.
      if (t.kind == Tok::Word) {
        if (tilde && !wantWord) units.push_back("~" + t.text);
        else units.push_back(t.text);
        tilde = false;
        wantWord = false;
        continue;
      }
      if (t.kind == Tok::Op && t.text == "~") {
        if (units.empty()) break;
        tilde = true;
        continue;
      }
      if (t.kind == Tok::Op && (t.text == "::" || t.text == ".")) {
        if (wantWord) break;
        units.push_back(t.text);
        wantWord = true;
        continue;
      }
      // Pointer/reference in the return type ('ICoreWebView2* Foo::get()').
      if (t.kind == Tok::Op && (t.text == "*" || t.text == "&")) continue;
      // Template arguments in the return type ('std::vector<int> Foo::f()'):
      // skip the balanced '<...>' (the arguments themselves are dropped).
      if (t.kind == Tok::Op && t.text == "<" && !wantWord) {
        int d = 0;
        for (; j < toks.size(); ++j) {
          if (toks[j].kind == Tok::Op && toks[j].text == "<") ++d;
          else if (toks[j].kind == Tok::Op && toks[j].text == ">") {
            --d;
            if (d == 0) break;
          }
        }
        if (j >= toks.size()) return false;
        continue;
      }
      break;
    }
    if (j >= toks.size() || toks[j].kind != Tok::Op || toks[j].text != "(")
      return false;

    // The last separator splits 'Class' from the member name; everything
    // before it is the return type ('std::string Foo::name()').
    size_t cut = 0;
    bool found = false;
    for (size_t k = units.size(); k-- > 1;) {
      if (units[k] == "::" || units[k] == ".") { cut = k; found = true; break; }
    }
    // The member name must be the last unit: 'std::ofstream out(path)' is a
    // global variable with a qualified type, not an out-of-line definition.
    if (!found || cut < 1 || cut + 2 != units.size()) return false;
    const std::string cls = units[cut - 1];
    const std::string name = units[cut + 1];
    std::string retTxt;
    for (size_t k = 0; k + 1 < cut; ++k) {
      const bool sep = (units[k] == "::" || units[k] == ".");
      const bool prevSep = k && (units[k - 1] == "::" || units[k - 1] == ".");
      if (k && !sep && !prevSep) retTxt += " ";
      retTxt += units[k];
    }
    std::shared_ptr<ClassDecl> target;
    for (auto& c : prog.classes) if (c->name == cls) { target = c; break; }
    if (!target) {
      // A namespace-qualified free function ('Ns::func()') is not a member;
      // leave it to the regular declaration paths.
      for (const std::string& ns : prog.usingNs)
        if (ns == cls) return false;
      for (const std::string& ns : nsNames_)
        if (ns == cls) return false;
      // The owner is defined in another translation unit (typically a
      // header). Synthesize the class so the method body is not lost.
      target = std::make_shared<ClassDecl>();
      target->name = cls;
      target->line = ln;
      prog.classes.push_back(target);
      Diag d;
      d.line = ln;
      d.msg = "out-of-line member '" + cls + "::" + name +
              "': class '" + cls + "' is not defined in this file; "
              "an empty class was synthesized for it";
      d.warning = true;
      diags.push_back(d);
    }
    pos = j;  // at '('
    auto M = std::make_shared<Method>();
    M->line = ln;
    M->mods = mods;
    M->name = name;
    if (!retTxt.empty()) {
      M->ret = std::make_shared<Type>();
      M->ret->base = retTxt;
    }
    if (name == cls) M->isCtor = true;
    if (!name.empty() && name[0] == '~') M->isDtor = true;
    expectOp("(");
    parseParams(M);
    expectOp(")");
    stripMethodTrailers(M);
    if (isOp(":")) parseCtorInitList(M);
    if (isOp("{")) M->body = parseBlock();
    else acceptOp(";");
    addClassMethod(target, M);
    return true;
  }

  void skipBalancedToParenEnd() {
    int depth = 0;
    while (!atEnd()) {
      if (isOp("(")) ++depth;
      else if (isOp(")")) {
        --depth;
        if (depth <= 0) { bump(); break; }
      }
      bump();
    }
    acceptOp(";");
  }

  // Register a top-level function, collapsing a declaration and its later
  // definition into one entry (the definition wins).
  void addFreeFunc(const std::shared_ptr<Method>& M) {
    std::vector<std::shared_ptr<Method>>& sink =
        nsStack_.empty() ? prog.freeFuncs : nsBucket().freeFuncs;
    for (auto& ex : sink) {
      if (sameSig(*ex, *M)) {
        if (M->body && !ex->body) ex = M;
        return;
      }
    }
    sink.push_back(M);
  }

  // Register a top-level variable, collapsing 'extern T x;' + 'T x = ...;'
  // (the initializer wins). Inside a named namespace body the variable goes
  // to that namespace's bucket (emitted as a holder class in Java).
  void addGlobal(const Field& F) {
    std::vector<Field>& sink = nsStack_.empty() ? prog.globals : nsBucket().globals;
    for (auto& ex : sink) {
      if (ex.name == F.name) {
        if (F.init && !ex.init) ex = F;
        else {
          Diag d;
          d.line = F.line;
          d.msg = "duplicate global '" + F.name + "' ignored (Java allows one field per name)";
          d.warning = true;
          diags.push_back(d);
        }
        return;
      }
    }
    sink.push_back(F);
  }

  // Bucket for the innermost enclosing named namespace (created on demand).
  NamespaceItems& nsBucket() {
    for (auto& ns : prog.namespaces)
      if (ns.parts == nsStack_) return ns;
    NamespaceItems ns;
    ns.parts = nsStack_;
    prog.namespaces.push_back(ns);
    return prog.namespaces.back();
  }

  void parseFreeFunction(std::vector<std::string> mods, std::vector<std::string> typeParams) {
    skipExportMacro();  // PLUGIN_API / DLLEXPORT / ... from an unread header
    int ln = cur().line;
    if (tryQualifiedMethod(ln, mods)) return;
    if (isWord("typedef")) {
      Diag d;
      d.line = ln;
      d.msg = "typedef skipped";
      d.warning = true;
      diags.push_back(d);
      skipUntilSemi();
      return;
    }
    TypePtr T;
    std::string name;
    try {
      T = parseType();
      skipCallSpecs();
      name = expectIdent();
    } catch (const ParseError&) {
      Diag d;
      d.line = ln;
      d.msg = "unsupported top-level declaration skipped";
      d.warning = true;
      diags.push_back(d);
      skipUntilSemi();
      if (isOp("{")) skipBalanced();
      return;
    }
    if (isOp("(")) {
      // 'Ret name(...)' is either a function or a variable with a
      // constructor-style initializer ('atomic<bool> ready(false);').
      // Parse the parameter list speculatively; when it is not a valid
      // parameter list, backtrack and read a variable initializer instead.
      size_t save = pos;
      auto M = std::make_shared<Method>();
      M->line = ln;
      M->mods = mods;
      M->typeParams = typeParams;
      M->ret = T;
      M->name = name;
      bool ok = true;
      try {
        bump();  // '('
        parseParams(M);
        expectOp(")");
      } catch (const ParseError&) {
        ok = false;
      }
      if (ok) {
        stripMethodTrailers(M);
        if (isOp("{")) M->body = parseBlock();
        else { M->body = nullptr; acceptOp(";"); }
        addFreeFunc(M);
        return;
      }
      pos = save;  // retry as 'Type name(args);'
    }

    // ---- top-level variable (now converted to a Java static field) ----
    try {
      Field F;
      F.mods = mods;
      F.type = T;
      F.name = name;
      F.line = ln;
      bool braceInit = false;
      parseDeclSuffixes(T, F.init, braceInit);
      if (!braceInit) {
        if (acceptOp("=")) F.init = parseExpr();
        else if (isOp("(")) {
          bump();
          auto e = std::make_shared<Expr>();
          e->k = Expr::K::Paren;
          e->line = ln;
          if (!acceptOp(")")) {
            do { e->args.push_back(parseExpr()); } while (acceptOp(","));
            expectOp(")");
          }
          F.init = e;
        }
      }
      addGlobal(F);
      while (acceptOp(",")) {
        Field F2;
        F2.mods = mods;
        F2.type = std::make_shared<Type>(*T);
        F2.type->dims = 0;
        F2.type->arrSize = nullptr;
        F2.name = expectIdent();
        F2.line = ln;
        bool bi = false;
        parseDeclSuffixes(F2.type, F2.init, bi);
        if (!bi) {
          if (acceptOp("=")) F2.init = parseExpr();
          else if (isOp("(")) {
            bump();
            auto e = std::make_shared<Expr>();
            e->k = Expr::K::Paren;
            e->line = ln;
            if (!acceptOp(")")) {
              do { e->args.push_back(parseExpr()); } while (acceptOp(","));
              expectOp(")");
            }
            F2.init = e;
          }
        }
        addGlobal(F2);
      }
      expectOp(";");
    } catch (const ParseError& e) {
      Diag d;
      d.line = ln;
      d.msg = std::string("unsupported top-level declaration skipped: ") + e.what();
      d.warning = true;
      diags.push_back(d);
      skipUntilSemi();
      if (isOp("{")) skipBalanced();
    }
  }

  std::shared_ptr<Program> run() {
    while (!atEnd()) {
      if (cur().kind == Tok::Eol) { bump(); continue; }
      skipAnnotations();
      if (isOp("#")) { parseDirective(); continue; }
      if (tryLinkageSpec()) continue;
      if (isWord("package")) { parsePackage(); continue; }
      if (isWord("import")) { parseImport(); continue; }
      if (isWord("using")) { parseUsing(); continue; }
      if (isWord("namespace")) { parseNamespace(); continue; }
      try {
        if (isWord("template")) {
          auto tp = parseTemplatePrefix();
          auto mods = parseModifiers();
          if (isWord("class") || isWord("struct") || isWord("interface")) {
            auto c = parseClass(mods, tp);
            if (c) prog.classes.push_back(c);
          } else {
            parseFreeFunction(mods, tp);
          }
          continue;
        }
        auto mods = parseModifiers();
        if (isWord("class") || isWord("struct") || isWord("interface")) {
          auto c = parseClass(mods, {});
          if (c) prog.classes.push_back(c);
        } else if (isWord("enum")) {
          prog.enums.push_back(parseEnum(mods));
        } else {
          parseFreeFunction(mods, {});
        }
      } catch (const ParseError& e) {
        // One malformed top-level declaration must not abort the file.
        Diag d;
        d.line = e.line;
        d.msg = std::string("top-level declaration skipped: ") + e.what();
        d.warning = true;
        diags.push_back(d);
        if (isOp("{")) {
          skipBalanced();
          continue;
        }
        if (!skipUntilSemi() && !isOp("}") && !atEnd()) bump();
      }
    }
    return std::make_shared<Program>(prog);
  }
};

} // namespace

// ---------------------------------------------------------------------------
// Parse entry point
// ---------------------------------------------------------------------------
std::shared_ptr<Program> parse(const std::string& text, std::vector<Diag>& diags) {
  PpResult pp = preprocessCpp(text, diags);
  std::vector<Token> toks = lex(pp.text);
  Parser p(toks, diags);
  std::shared_ptr<Program> prog = p.run();
  for (auto& inc : pp.includes) prog->includes.push_back(inc);
  return prog;
}

} // namespace bridge