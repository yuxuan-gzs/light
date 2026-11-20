// lexer.cpp - tokenizer shared by both Java and C++ dialects.
#include "bridge.hpp"
#include <fstream>
#include <cctype>
#include <cstdlib>

namespace bridge {

// ---------------------------------------------------------------------------
// String utilities
// ---------------------------------------------------------------------------
std::string trimWs(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

std::vector<std::string> splitStr(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) { out.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  out.push_back(cur);
  return out;
}

std::string joinStr(const std::vector<std::string>& parts, const std::string& sep) {
  std::string r;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i) r += sep;
    r += parts[i];
  }
  return r;
}

bool startsWith(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool endsWith(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

std::string toLower(const std::string& s) {
  std::string r = s;
  for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return r;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
  if (from.empty()) return s;
  size_t pos = 0;
  while ((pos = s.find(from, pos)) != std::string::npos) {
    s.replace(pos, from.size(), to);
    pos += to.size();
  }
  return s;
}

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------
static bool isIdStart(char c) { return std::isalpha(static_cast<unsigned char>(c)) || c == '_'; }
static bool isIdChar(char c)  { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
static bool isDigit(char c)   { return c >= '0' && c <= '9'; }
static bool isHex(char c)     { return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

namespace {
Token mk(Tok k, const std::string& t, int line) {
  Token tok;
  tok.kind = k;
  tok.text = t;
  tok.line = line;
  return tok;
}
} // namespace

std::vector<Token> lex(const std::string& text) {
  std::vector<Token> toks;
  size_t i = 0;
  const size_t n = text.size();
  int line = 1;

  // strip UTF-8 BOM
  if (n >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB &&
      static_cast<unsigned char>(text[2]) == 0xBF) {
    i = 3;
  }

  while (i < n) {
    char c = text[i];

    if (c == '\n') {
      toks.push_back(mk(Tok::Eol, "\n", line));
      ++line;
      ++i;
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

    // line comment
    if (c == '/' && i + 1 < n && text[i + 1] == '/') {
      while (i < n && text[i] != '\n') ++i;
      continue;
    }
    // block comment
    if (c == '/' && i + 1 < n && text[i + 1] == '*') {
      i += 2;
      while (i + 1 < n && !(text[i] == '*' && text[i + 1] == '/')) {
        if (text[i] == '\n') ++line;
        ++i;
      }
      if (i + 1 >= n) throw ParseError(line, "unterminated block comment");
      i += 2;
      continue;
    }

    // Wide/unicode literal prefixes: L"x" / u"x" / U"x" / u8"x" and the
    // character forms L'c' / u'c' / U'c'. Java has no prefixed literals
    // (String is UTF-16, char is UTF-16), so the prefix is dropped and the
    // payload is lexed as a plain string/char literal.
    if ((c == 'L' || c == 'u' || c == 'U') && i + 1 < n &&
        (text[i + 1] == '"' || text[i + 1] == '\'')) {
      ++i;  // skip the single-letter prefix
      c = text[i];
    } else if (c == 'u' && i + 2 < n && text[i + 1] == '8' &&
               (text[i + 2] == '"' || text[i + 2] == '\'')) {
      i += 2;  // skip the 'u8' prefix
      c = text[i];
    }

    // identifier / keyword
    if (isIdStart(c)) {
      size_t s = i;
      while (i < n && isIdChar(text[i])) ++i;
      std::string w = text.substr(s, i - s);
      if (w == "true" || w == "false") toks.push_back(mk(Tok::Bool, w, line));
      else if (w == "null" || w == "nullptr") toks.push_back(mk(Tok::Null, "null", line));
      else toks.push_back(mk(Tok::Word, w, line));
      continue;
    }

    // numbers
    if (isDigit(c) || (c == '.' && i + 1 < n && isDigit(text[i + 1]))) {
      size_t s = i;
      bool isFloat = false;
      if (c == '0' && i + 1 < n && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
        i += 2;
        while (i < n && isHex(text[i])) ++i;
        // integer suffixes
        while (i < n && (text[i] == 'u' || text[i] == 'U' || text[i] == 'l' || text[i] == 'L')) ++i;
      } else {
        while (i < n && isDigit(text[i])) ++i;
        if (i < n && text[i] == '.') {
          isFloat = true;
          ++i;
          while (i < n && isDigit(text[i])) ++i;
        }
        if (i < n && (text[i] == 'e' || text[i] == 'E')) {
          isFloat = true;
          ++i;
          if (i < n && (text[i] == '+' || text[i] == '-')) ++i;
          while (i < n && isDigit(text[i])) ++i;
        }
        while (i < n && (text[i] == 'u' || text[i] == 'U' || text[i] == 'l' || text[i] == 'L' ||
                         text[i] == 'f' || text[i] == 'F' || text[i] == 'd' || text[i] == 'D')) {
          char sfx = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
          if (sfx == 'f' || sfx == 'd') isFloat = true;
          ++i;
        }
      }
      toks.push_back(mk(isFloat ? Tok::Float : Tok::Int, text.substr(s, i - s), line));
      continue;
    }

    // string literal
    if (c == '"') {
      ++i;
      std::string payload;
      bool closed = false;
      while (i < n) {
        if (text[i] == '\\' && i + 1 < n) {
          payload.push_back('\\');
          payload.push_back(text[i + 1]);
          i += 2;
          continue;
        }
        if (text[i] == '"') { closed = true; ++i; break; }
        if (text[i] == '\n') { ++line; }
        payload.push_back(text[i]);
        ++i;
      }
      if (!closed) throw ParseError(line, "unterminated string literal");
      toks.push_back(mk(Tok::Str, payload, line));
      continue;
    }

    // char literal
    if (c == '\'') {
      ++i;
      std::string payload;
      bool closed = false;
      while (i < n) {
        if (text[i] == '\\' && i + 1 < n) {
          payload.push_back('\\');
          payload.push_back(text[i + 1]);
          i += 2;
          continue;
        }
        if (text[i] == '\'') { closed = true; ++i; break; }
        if (text[i] == '\n') ++line;
        payload.push_back(text[i]);
        ++i;
      }
      if (!closed) throw ParseError(line, "unterminated char literal");
      toks.push_back(mk(Tok::Char, payload, line));
      continue;
    }

    // operators / punctuation (longest match first)
    static const char* kOpsLong[] = {
      "...", "->", "::", "==", "!=", "<=", ">=", "&&", "||",
      "++", "--", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<"
    };
    bool matched = false;
    for (const char* op : kOpsLong) {
      size_t L = std::string(op).size();
      if (i + L <= n && text.compare(i, L, op) == 0) {
        toks.push_back(mk(Tok::Op, op, line));
        i += L;
        matched = true;
        break;
      }
    }
    if (matched) continue;

    // '>' may be '>=', or split '>>' / '>>>' for generics support
    if (c == '>') {
      if (i + 1 < n && text[i + 1] == '=') { toks.push_back(mk(Tok::Op, ">=", line)); i += 2; }
      else { toks.push_back(mk(Tok::Op, ">", line)); ++i; }
      continue;
    }
    if (c == '<') {
      if (i + 1 < n && text[i + 1] == '=') { toks.push_back(mk(Tok::Op, "<=", line)); i += 2; }
      else if (i + 1 < n && text[i + 1] == '<') { toks.push_back(mk(Tok::Op, "<<", line)); i += 2; }
      else { toks.push_back(mk(Tok::Op, "<", line)); ++i; }
      continue;
    }

    static const char kSingle[] = "+-*/%<>=!&|^~?:.,;()[]{}@";
    if (std::strchr(kSingle, c)) {
      toks.push_back(mk(Tok::Op, std::string(1, c), line));
      ++i;
      continue;
    }

    if (c == '#') {
      toks.push_back(mk(Tok::Op, "#", line));
      ++i;
      continue;
    }

    throw ParseError(line, std::string("unexpected character '") + c + "'");
  }

  toks.push_back(mk(Tok::End, "", line));
  return toks;
}

} // namespace bridge