// gen_c2j.cpp - C++ -> Java code generator (T5).
// Translates the shared AST (C++17 flavour, after parsing real C++ sources)
// into a single .java translation unit. Conversions are best-effort; the
// exact approximations are documented in docs/README.md (known limitations).
#include "bridge.hpp"
#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>

namespace bridge {

namespace {

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
bool hasMod(const std::vector<std::string>& mods, const std::string& m) {
  return std::find(mods.begin(), mods.end(), m) != mods.end();
}

std::string lastSegName(const std::string& s) {
  size_t p = s.find_last_of("::");
  if (p == std::string::npos) p = s.find_last_of('.');
  return (p == std::string::npos) ? s : s.substr(p + 1);
}

bool isStdStr(const TypePtr& t) {
  if (!t || t->dims > 0) return false;
  std::string b = lastSegName(t->base);
  return b == "string" || b == "string_view";
}
bool listLikeT(const TypePtr& t) {
  if (!t || t->dims > 0) return false;
  static const std::set<std::string> n = {"vector", "list", "deque"};
  return n.count(lastSegName(t->base)) != 0;
}
bool mapLikeT(const TypePtr& t) {
  if (!t || t->dims > 0) return false;
  static const std::set<std::string> n = {"map", "unordered_map"};
  return n.count(lastSegName(t->base)) != 0;
}
bool setLikeT(const TypePtr& t) {
  if (!t || t->dims > 0) return false;
  static const std::set<std::string> n = {"set", "unordered_set"};
  return n.count(lastSegName(t->base)) != 0;
}
bool collT(const TypePtr& t) { return listLikeT(t) || mapLikeT(t) || setLikeT(t); }

// Recursively searches an expression tree for a bare identifier.
bool exprMentions(const ExprPtr& e, const std::string& n) {
  if (!e) return false;
  if (e->k == Expr::K::Name) return e->name == n;
  bool r = false;
  if (e->obj) r = r || exprMentions(e->obj, n);
  if (e->lhs) r = r || exprMentions(e->lhs, n);
  if (e->rhs) r = r || exprMentions(e->rhs, n);
  if (e->callee) r = r || exprMentions(e->callee, n);
  if (e->index) r = r || exprMentions(e->index, n);
  if (e->sizeEx) r = r || exprMentions(e->sizeEx, n);
  for (auto& a : e->args) r = r || exprMentions(a, n);
  return r;
}

bool isStdSym(const ExprPtr& e, const std::string& n) {
  return e && e->k == Expr::K::Scope && e->name == n && e->obj &&
         e->obj->k == Expr::K::Name && e->obj->name == "std";
}

// Flattens "a << b << c << std::endl" into segments {b, c} + flags.
bool splitCout(const ExprPtr& e, std::vector<ExprPtr>& segs,
               bool& endsEndl, bool& cerrOut) {
  if (!e) return false;
  if (e->k == Expr::K::Binary && e->op == "<<") {
    bool ok = false;
    if (isStdSym(e->rhs, "endl")) endsEndl = true;
    // Recurse into the left side first so segments keep source order:
    // "a << b << c" flattens to {a, b, c}, not {c, b}.
    if (splitCout(e->lhs, segs, endsEndl, cerrOut)) ok = true;
    if (!isStdSym(e->rhs, "endl") && !isStdSym(e->rhs, "flush"))
      segs.push_back(e->rhs);
    return ok;
  }
  if (isStdSym(e, "cout")) return true;
  if (isStdSym(e, "cerr")) { cerrOut = true; return true; }
  return false;
}

std::string fltLit(const std::string& txt) {
  // Keep the original literal text; ensure a decimal point / exponent is
  // present so Java treats it as a double rather than an int.
  if (txt.find_first_of(".eE") == std::string::npos) return txt + ".0";
  return txt;
}

// Flattens "a::b::c" / "a.b.c" scope chains into "a.b.c".
std::string scopeDots(const ExprPtr& e) {
  if (e->k == Expr::K::Name) return e->name;
  if (e->k == Expr::K::Scope && e->obj) {
    std::string o = scopeDots(e->obj);
    if (o == "std") return e->name;  // bare std::xxx (caller decides)
    return o + "." + e->name;
  }
  return e->name;
}

// ---------------------------------------------------------------------------
// Emitter state: generator context + per-translation flags.
// ---------------------------------------------------------------------------
struct E {
  GenCtx& ctx;
  bool inMain = false;  // inside translated main()
  int curIndent = 0;    // indentation of the statement being emitted
  // Holder class of the namespace whose body is being emitted ("" at file
  // scope). Bare references to that namespace's globals/free functions are
  // emitted as 'Holder.name' so they still resolve once the namespace became
  // a class of its own.
  std::string nsHolder;
  bool inEnumBody = false;  // emitting an enum's constant list
};

// Resolves a bare name against the entity currently being emitted:
//   * an enum constant becomes 'Enum.CONST' (Java scoping), except inside
//     the enum's own constant list;
//   * a name that really lives in the enclosing C++ namespace's holder class
//     becomes 'Holder.name', because that namespace became a class.
std::string qualName(const std::string& n, E& em) {
  if (!em.inEnumBody) {
    auto it = em.ctx.enumConstOwner.find(n);
    if (it != em.ctx.enumConstOwner.end()) return it->second + "." + n;
  }
  if (!em.nsHolder.empty()) {
    auto ns = em.ctx.nsMembers.find(em.nsHolder);
    if (ns != em.ctx.nsMembers.end() && ns->second.count(n))
      return em.nsHolder + "." + sanitizeJavaName(n);
  }
  return sanitizeJavaName(n);
}

// ---------------------------------------------------------------------------
// Java type name (std exceptions get mapped to Java runtime exceptions).
// ---------------------------------------------------------------------------
std::string tn(const TypePtr& t, GenCtx& ctx, int line) {
  if (!t) return "Object";
  const std::string& b = t->base;
  if (b == "std::runtime_error" || b == "std::exception" ||
      b == "std::range_error" || b == "std::overflow_error" ||
      b == "std::underflow_error" || b == "std::bad_alloc" ||
      b == "std::bad_typeid")
    return "RuntimeException";
  if (b == "std::logic_error" || b == "std::length_error" ||
      b == "std::invalid_argument" || b == "std::domain_error" ||
      b == "std::out_of_range" || b == "std::ios_base::failure")
    return "IllegalArgumentException";
  if (b == "std::bad_cast") return "ClassCastException";
  if (b == "void") return "void";
  if (b == "auto" || b.empty()) return "var";
  std::string r = mapC2J(*t, ctx.typeParams);
  if (r == "auto") return "var";
  if (startsWith(r, "std::") || r.find("::") != std::string::npos) {
    if (line > 0)
      ctx.warn(line, "unmapped C++ type '" + r + "' approximated to '" + lastSegName(r) + "'");
    return lastSegName(r);
  }
  return r;
}

// ---------------------------------------------------------------------------
// Expression emitter (best-effort). Compound operators wrap their operands
// in parentheses; leaf expressions (names, literals, calls) stay bare.
// ---------------------------------------------------------------------------
std::string emitCall(const ExprPtr& e, E& em);
std::string emitLambdaBody(const ExprPtr& e, E& em);
bool emitStmt(const StmtPtr& s, E& em, Out& out);

std::string emitExpr(const ExprPtr& e, E& em) {
  if (!e) return "";
  switch (e->k) {
    case Expr::K::Name: return qualName(e->name, em);
    case Expr::K::This: return "this";
    case Expr::K::Null: return "null";
    case Expr::K::BoolLit: return e->bval ? "true" : "false";
    case Expr::K::IntLit: return e->sval;
    case Expr::K::FloatLit: return fltLit(e->sval);
    case Expr::K::StrLit: return "\"" + litEscape(e->sval) + "\"";
    case Expr::K::CharLit: return "'" + litEscape(e->sval) + "'";
    case Expr::K::Paren: return "(" + emitExpr(e->lhs, em) + ")";

    case Expr::K::Unary: {
      const std::string& op = e->op;
      if (op == "&") return emitExpr(e->lhs, em);                 // address-of -> value
      if (op == "*") return "(" + emitExpr(e->lhs, em) + "[0])";  // deref -> [0]
      if (op == "!") return "!(" + emitExpr(e->lhs, em) + ")";
      if (op == "-") return "-(" + emitExpr(e->lhs, em) + ")";
      if (op == "+") return "+(" + emitExpr(e->lhs, em) + ")";
      if (op == "~") return "~(" + emitExpr(e->lhs, em) + ")";
      em.ctx.warn(e->line, "unary operator '" + op + "' approximated");
      return op + "(" + emitExpr(e->lhs, em) + ")";
    }
    case Expr::K::IncDec: {
      std::string x = emitExpr(e->lhs, em);
      return e->isPrefix ? e->op + x : x + e->op;
    }
    case Expr::K::Binary: {
      return "(" + emitExpr(e->lhs, em) + " " + e->op + " " +
             emitExpr(e->rhs, em) + ")";
    }
    case Expr::K::Assign: {
      const ExprPtr& lhs = e->lhs;
      if (lhs->k == Expr::K::Index) {
        TypePtr rt = inferJavaType(lhs->obj, em.ctx);
        std::string recv = emitExpr(lhs->obj, em);
        std::string idx = emitExpr(lhs->index, em);
        // Java: List.set(int,E) but Map.put(K,V) - std::map has no .set().
        if (mapLikeT(rt)) {
          if (e->op == "=")
            return recv + ".put(" + idx + ", " + emitExpr(e->rhs, em) + ")";
          std::string op2 = e->op.substr(0, 1);
          return recv + ".put(" + idx + ", " + recv + ".get(" + idx + ") " +
                 op2 + " " + emitExpr(e->rhs, em) + ")";
        }
        if (listLikeT(rt)) {
          if (e->op == "=")
            return recv + ".set(" + idx + ", " + emitExpr(e->rhs, em) + ")";
          std::string op2 = e->op.substr(0, 1);
          return recv + ".set(" + idx + ", " + recv + ".get(" + idx + ") " +
                 op2 + " " + emitExpr(e->rhs, em) + ")";
        }
        if (isStdStr(rt)) {
          em.ctx.warn(e->line, "std::string index write not supported (String is immutable)");
          return "null";
        }
      }
      return "(" + emitExpr(lhs, em) + " " + e->op + " " +
             emitExpr(e->rhs, em) + ")";
    }
    case Expr::K::Call:
      return emitCall(e, em);
    case Expr::K::Member: {
      std::string recv = emitExpr(e->obj, em);
      if (recv == "this") return "this." + e->name;
      return recv + "." + e->name;
    }
    case Expr::K::Arrow: {
      // '->' becomes value semantics '.' (pointers/references approximated)
      std::string recv = emitExpr(e->obj, em);
      if (recv == "this") return "this." + e->name;
      return recv + "." + e->name;
    }
    case Expr::K::Scope: {
      if (e->obj && e->obj->k == Expr::K::Name && e->obj->name == "std") {
        if (e->name == "cout") return "System.out";
        if (e->name == "cerr") return "System.err";
        if (e->name == "endl") return "\"\\n\"";
        em.ctx.warn(e->line, "std::" + e->name + " used as expression");
        return e->name;
      }
      return scopeDots(e);
    }
    case Expr::K::Index: {
      TypePtr rt = inferJavaType(e->obj, em.ctx);
      std::string recv = emitExpr(e->obj, em);
      std::string idx = emitExpr(e->index, em);
      if (isStdStr(rt)) return recv + ".charAt(" + idx + ")";
      if (listLikeT(rt) || mapLikeT(rt)) return recv + ".get(" + idx + ")";
      return recv + "[" + idx + "]";  // plain arrays map to Java []
    }
    case Expr::K::NewObj: {
      std::string t = tn(e->type, em.ctx, e->line);
      std::string args;
      for (size_t i = 0; i < e->args.size(); ++i) {
        if (i) args += ", ";
        args += emitExpr(e->args[i], em);
      }
      return "new " + t + "(" + args + ")";
    }
    case Expr::K::NewArr: {
      std::string tnTxt = "Object";
      if (e->type) {
        Type copy = *e->type;
        copy.dims = 0;
        copy.isPtr = false;
        copy.arrSize = nullptr;
        tnTxt = tn(std::make_shared<Type>(copy), em.ctx, e->line);
      }
      if (e->sizeEx) return "new " + tnTxt + "[" + emitExpr(e->sizeEx, em) + "]";
      return "new " + tnTxt + "[0]";
    }
    case Expr::K::Cast: {
      if (e->type && e->type->base == "void") {
        // '(void)x;' statement: nothing to emit
        em.ctx.warn(e->line, "cast to void dropped");
        return "0";
      }
      // An enum has no numeric cast in Java; 'ordinal()' is the closest
      // equivalent (note: explicit C++ enumerator values are dropped, so the
      // number may differ from the original).
      if (e->type) {
        const std::string& b = e->type->base;
        if (b == "int" || b == "long" || b == "short" || b == "char" ||
            b == "unsigned" || b == "unsigned int" || b == "size_t") {
          TypePtr ot = inferJavaType(e->lhs, em.ctx);
          if (ot && em.ctx.enumNames.count(ot->base)) {
            em.ctx.warn(e->line, "enum -> " + b + " cast emitted as '.ordinal()'");
            return "(" + emitExpr(e->lhs, em) + ".ordinal())";
          }
        }
      }
      return "((" + tn(e->type, em.ctx, e->line) + ") " +
             emitExpr(e->lhs, em) + ")";
    }
    case Expr::K::Ternary: {
      std::string els =
          e->args.empty() ? emitExpr(e->rhs, em) : emitExpr(e->args[0], em);
      return "(" + emitExpr(e->lhs, em) + " ? " + emitExpr(e->rhs, em) +
             " : " + els + ")";
    }
    case Expr::K::ArrayInit: {
      em.ctx.warn(e->line, "braced init list used as a standalone expression");
      std::string s;
      for (size_t i = 0; i < e->args.size(); ++i) {
        if (i) s += ", ";
        s += emitExpr(e->args[i], em);
      }
      return "{" + s + "}";
    }
    case Expr::K::Lambda: {
      // C++ lambda -> Java lambda; captures have no Java equivalent.
      if (!e->lcapture.empty())
        em.ctx.warn(e->line, "lambda capture '" + e->lcapture +
                                 "' dropped (Java captures effectively-final locals)");
      std::string ps;
      for (size_t i = 0; i < e->lparams.size(); ++i) {
        if (i) ps += ", ";
        const LambdaP& lp = e->lparams[i];
        ps += (lp.type ? tn(lp.type, em.ctx, e->line) : std::string("var")) + " " +
              sanitizeJavaName(lp.name);
      }
      std::string bodyTxt = emitLambdaBody(e, em);
      return "(" + ps + ") -> {" + bodyTxt +
             std::string(static_cast<size_t>(em.curIndent) * 2u, ' ') + "}";
    }
  }
  return "";
}

std::string emitCall(const ExprPtr& e, E& em) {
  const ExprPtr& c = e->callee;
  std::string args;
  for (size_t i = 0; i < e->args.size(); ++i) {
    if (i) args += ", ";
    args += emitExpr(e->args[i], em);
  }
  if (!c) { em.ctx.warn(e->line, "malformed call"); return "(" + args + ")"; }

  // ---- std::xxx free functions ----
  if (c->k == Expr::K::Scope && c->obj && c->obj->k == Expr::K::Name &&
      c->obj->name == "std") {
    const std::string& n = c->name;
    if (n == "stoi") return "Integer.parseInt(" + args + ")";
    if (n == "stol" || n == "stoul" || n == "stoll")
      return "Long.parseLong(" + args + ")";
    if (n == "stod") return "Double.parseDouble(" + args + ")";
    if (n == "stof") return "Float.parseFloat(" + args + ")";
    if (n == "to_string") return "String.valueOf(" + args + ")";
    if (n == "abs") return "Math.abs(" + args + ")";
    if (n == "max") return "Math.max(" + args + ")";
    if (n == "min") return "Math.min(" + args + ")";
    if (n == "move" || n == "forward") {
      if (e->args.empty()) return "null";
      return emitExpr(e->args[0], em);
    }
    if (n == "make_shared" || n == "make_unique") {
      em.ctx.warn(e->line,
                  "std::" + n + "<T>(...) template args unsupported; use explicit T(...) in input");
      return "new Object()";
    }
    if (n == "swap") {
      em.ctx.warn(e->line, "std::swap has no single-call Java equivalent");
      return "null";
    }
    if (n == "getline" || n == "cin" || n == "cout" || n == "cerr") {
      em.ctx.warn(e->line, "console/stream I/O '" + n + "' not supported");
      return "null";
    }
    if (n == "string" || n == "wstring" || n == "u16string" ||
        n == "u32string") {
      // std::string(x) is a conversion/constructor: Java's String.valueOf
      // accepts any operand and still concatenates correctly.
      if (e->args.size() == 1) return "String.valueOf(" + args + ")";
      em.ctx.warn(e->line, "std::" + n + "(...) with " +
                               std::to_string(e->args.size()) +
                               " arguments has no Java equivalent; emitted as \"\"");
      return "\"\"";
    }
    em.ctx.warn(e->line, "std::" + n + " kept as-is (unmapped)");
    return n + "(" + args + ")";
  }

  // ---- Class-static call: Math::max, Foo::bar -> Math.max, Foo.bar ----
  if (c->k == Expr::K::Scope && c->obj) {
    return scopeDots(c) + "(" + args + ")";
  }

  // ---- Method call on an object (member / arrow) ----
  if (c->k == Expr::K::Member || c->k == Expr::K::Arrow) {
    const std::string& n = c->name;
    std::string recv = emitExpr(c->obj, em);
    TypePtr rt = inferJavaType(c->obj, em.ctx);
    if (n == "push_back" && listLikeT(rt)) return recv + ".add(" + args + ")";
    if (n == "at" && (listLikeT(rt) || mapLikeT(rt)))
      return recv + ".get(" + args + ")";
    if (n == "front" && listLikeT(rt)) return recv + ".get(0)";
    if (n == "back" && listLikeT(rt))
      return recv + ".get(" + recv + ".size() - 1)";
    if (n == "size")
      return isStdStr(rt) ? recv + ".length()" : recv + ".size()";
    if (n == "length") return recv + ".length()";
    if (n == "empty") return recv + ".isEmpty()";
    if (n == "substr" && isStdStr(rt)) return recv + ".substring(" + args + ")";
    if (n == "compare" && isStdStr(rt)) return recv + ".compareTo(" + args + ")";
    if (n == "find") {
      if (isStdStr(rt)) return recv + ".indexOf(" + args + ")";
      if (mapLikeT(rt)) return recv + ".containsKey(" + args + ")";
    }
    if (n == "append" && isStdStr(rt)) {
      if (e->args.empty()) return recv;
      return "(" + recv + " + " + emitExpr(e->args[0], em) + ")";
    }
    if (n == "insert" && mapLikeT(rt)) {
      if (e->args.size() == 2) return recv + ".put(" + args + ")";
      const ExprPtr& a0 = e->args.empty() ? nullptr : e->args[0];
      if (a0 && a0->k == Expr::K::ArrayInit && a0->args.size() == 2)
        return recv + ".put(" + emitExpr(a0->args[0], em) + ", " +
               emitExpr(a0->args[1], em) + ")";
      if (a0 && a0->k == Expr::K::Call && a0->callee &&
          a0->callee->k == Expr::K::Scope && a0->callee->name == "make_pair" &&
          a0->args.size() >= 2)
        return recv + ".put(" + emitExpr(a0->args[0], em) + ", " +
               emitExpr(a0->args[1], em) + ")";
      em.ctx.warn(e->line, "map insert form approximated as put(key,value)");
      return recv + ".put(" + args + ")";
    }
    if (n == "insert" && listLikeT(rt)) return recv + ".add(" + args + ")";
    if (n == "contains") {
      if (mapLikeT(rt)) return recv + ".containsKey(" + args + ")";
      return recv + ".contains(" + args + ")";
    }
    if (n == "erase") {
      if (mapLikeT(rt) || setLikeT(rt) || listLikeT(rt))
        return recv + ".remove(" + args + ")";
    }
    if (n == "clear") return recv + ".clear()";
    if (n == "c_str") {
      em.ctx.warn(e->line, "c_str() approximated to null");
      return "null";
    }
    if (n == "begin" || n == "end") {
      em.ctx.warn(e->line, "iterator begin()/end() unsupported; prefer range-for");
      return recv + "." + n + "()";
    }
    if (n == "top" || n == "pop" || n == "pop_back" || n == "pop_front") {
      em.ctx.warn(e->line, "container op '" + n + "' approximated: remove");
      return recv + ".remove(" + recv + ".size() - 1)";
    }
    return recv + "." + n + "(" + args + ")";
  }

  // ---- Free function / constructor by bare name ----
  if (c->k == Expr::K::Name) {
    const std::string& n = c->name;
    if (n == "sizeof") {
      em.ctx.warn(e->line, "sizeof has no Java equivalent; emitted as 0");
      return "0";
    }
    bool isCtor = false;
    if (em.ctx.prog) {
      for (auto& cld : em.ctx.prog->classes)
        if (cld->name == n) { isCtor = true; break; }
      if (!isCtor)
        for (auto& en : em.ctx.prog->enums)
          if (en->name == n) { isCtor = true; break; }
    }
    if (isCtor) {  // Foo(x) -> new Foo(x)
      em.ctx.warn(e->line, "call '" + n + "(...)' treated as constructor (-> new)");
      return "new " + n + "(" + args + ")";
    }
    if (em.ctx.prog) {
      for (auto& ff : em.ctx.prog->freeFuncs)
        if (ff->name == n) {
          em.ctx.warn(e->line, "free function '" + n + "' has no Java equivalent; kept as-is");
          break;
        }
    }
    return qualName(n, em) + "(" + args + ")";
  }

  em.ctx.warn(e->line, "unsupported call target; emitted as-is");
  return "(" + emitExpr(c, em) + ")(" + args + ")";
}

// ---------------------------------------------------------------------------
// Variable declaration (special cases: main argv reconstruction, braced
// init lists, float literals assigned to float variables).
// ---------------------------------------------------------------------------
std::string initVal(const ExprPtr& init, const TypePtr& type, E& em) {
  // C++ direct-init syntax: 'Foo<T> x(args)' arrives as a Paren node whose
  // arguments live in .args; emit it as 'new T(args)'.
  if (init->k == Expr::K::Paren && !init->args.empty()) {
    std::string t = "Object";
    if (type) {
      Type copy = *type;
      copy.dims = 0;
      copy.isPtr = false;
      copy.arrSize = nullptr;
      t = tn(std::make_shared<Type>(copy), em.ctx, init->line);
    }
    std::string s;
    for (size_t i = 0; i < init->args.size(); ++i) {
      if (i) s += ", ";
      s += emitExpr(init->args[i], em);
    }
    // Java primitives / String cannot be 'new'-ed: 'bool ready(false)'
    // degenerates to a plain parenthesized expression '(false)'.
    static const std::set<std::string> plain = {
        "int", "long", "short", "byte", "char", "boolean", "float", "double",
        "String"};
    if (plain.count(t)) return "(" + s + ")";
    return "new " + t + "(" + s + ")";
  }
  if (init->k == Expr::K::ArrayInit) {
    if (collT(type)) {
      if (listLikeT(type) || setLikeT(type)) {
        std::string s;
        for (size_t i = 0; i < init->args.size(); ++i) {
          if (i) s += ", ";
          s += emitExpr(init->args[i], em);
        }
        return "new ArrayList<>(java.util.Arrays.asList(" + s + "))";
      }
      em.ctx.warn(init->line, "map braced-init list dropped; use insert()");
      return "new HashMap<>()";
    }
    // plain array: int a[] = {1,2,3} -> new int[]{1,2,3}
    std::string t = "Object";
    if (type) {
      Type copy = *type;
      copy.dims = 0;
      copy.isPtr = false;
      copy.arrSize = nullptr;
      t = tn(std::make_shared<Type>(copy), em.ctx, init->line);
    }
    std::string s;
    for (size_t i = 0; i < init->args.size(); ++i) {
      if (i) s += ", ";
      s += emitExpr(init->args[i], em);
    }
    return "new " + t + "[]{" + s + "}";
  }
  std::string v = emitExpr(init, em);
  if (init->k == Expr::K::FloatLit && type && lastSegName(type->base) == "float")
    v += "f";
  return v;
}

// A safe default initializer for locals/static fields (Java requires locals
// to be definitely assigned before use; C++ has no such rule).
std::string defaultLocalInit(const TypePtr& t, int line, GenCtx& ctx) {
  if (!t) return "";
  if (collT(t)) {
    if (listLikeT(t)) return "new ArrayList<>()";
    if (setLikeT(t)) return "new HashSet<>()";
    return "new HashMap<>()";
  }
  if (isStdStr(t)) return "\"\"";
  std::string b = lastSegName(t->base);
  if (b == "int" || b == "short" || b == "byte" || b == "long") return "0";
  if (b == "double" || b == "float") return "0.0";
  if (b == "char") return "'\\0'";
  if (b == "bool") return "false";
  if (b == "optional") return "Optional.empty()";
  // Raw pointer / reference may legitimately be null in C++; keep Java null.
  if (t->isPtr || t->isRef) return "null";
  // User class: construct it instead of null so later use does not NPE.
  // Interface-like classes (no fields, all methods pure) can't be
  // instantiated; approximate those with null.
  if (ctx.prog) {
    for (auto& c : ctx.prog->classes) {
      if (c->name != b) continue;
      bool hasData = !c->fields.empty();
      bool hasCtorImpl = false;
      size_t methodCount = 0, pureCount = 0;
      for (auto& m : c->methods) {
        if (m.isDtor) continue;
        if (m.isCtor) { hasCtorImpl = true; continue; }
        ++methodCount;
        if (hasMod(m.mods, "pure")) ++pureCount;
      }
      bool iface = !hasData && !hasCtorImpl && methodCount > 0 &&
                   pureCount == methodCount;
      if (iface) return "null";
      std::string arg;
      for (auto& tp : c->typeParams) {
        if (!arg.empty()) arg += ", ";
        arg += tp;
      }
      ctx.warn(line, "default-constructed '" + b + "' approximated as 'new " + b +
                         "()' assuming an implicit default constructor");
      return "new " + b + (arg.empty() ? "()" : "<>()");
    }
  }
  (void)line;
  return "null";
}

void emitVarDecl(const VarDeclS& vd, E& em, Out& out) {
  const TypePtr& t = vd.type;
  bool isAuto = !t || lastSegName(t->base) == "auto";
  std::string tnTxt = isAuto ? "var " : tn(t, em.ctx, 0) + " ";
  for (auto& d : vd.decls) {
    std::string nm = sanitizeJavaName(d.name);
    std::string initTxt;
    if (!d.init) {
      initTxt = defaultLocalInit(t, 0, em.ctx);
      if (!initTxt.empty()) initTxt = " = " + initTxt;
    } else if (em.inMain && nm == "args" && listLikeT(t) &&
               exprMentions(d.init, "argv")) {
      // C++ "std::vector<std::string> args(argv+1, argv+argc);" ->
      // Java "var args = java.util.Arrays.asList(argv).subList(1, argv.length);"
      initTxt = " = java.util.Arrays.asList(argv).subList(1, argv.length)";
    } else {
      initTxt = " = " + initVal(d.init, t, em);
    }
    out.line(tnTxt + nm + initTxt + ";");
    em.ctx.locals.add(d.name, t);
  }
}

// ---------------------------------------------------------------------------
// Statement emitter.
// ---------------------------------------------------------------------------
// Emit a lambda body into a scratch buffer indented one level past the
// statement that contains the lambda, so the spliced text lines up.
std::string emitLambdaBody(const ExprPtr& e, E& em) {
  Out tmp;
  tmp.indent = em.curIndent + 1;
  em.ctx.locals.push();
  for (auto& lp : e->lparams) em.ctx.locals.add(lp.name, lp.type);
  if (e->lbody) {
    for (auto& st : e->lbody->stmts) emitStmt(st, em, tmp);
  }
  em.ctx.locals.pop();
  std::string s = tmp.str();
  while (!s.empty() && s.back() == '\n') s.pop_back();
  return s;
}

bool emitStmt(const StmtPtr& s, E& em, Out& out);

void emitBlock(const BlockPtr& b, E& em, Out& out) {
  em.ctx.locals.push();
  for (auto& st : b->stmts) emitStmt(st, em, out);
  em.ctx.locals.pop();
}

bool emitStmt(const StmtPtr& s, E& em, Out& out) {
  if (!s) return false;
  em.curIndent = out.indent;
  switch (s->k) {
    case Stmt::K::Block: {
      emitBlock(s->block, em, out);
      return true;
    }
    case Stmt::K::VarDecl: {
      emitVarDecl(s->vdecl, em, out);
      return true;
    }
    case Stmt::K::Expr: {
      // std::cout / std::cerr stream statement?
      std::vector<ExprPtr> segs;
      bool endsEndl = false, isCerr = false;
      if (splitCout(s->expr, segs, endsEndl, isCerr)) {
        std::string joined;
        for (size_t i = 0; i < segs.size(); ++i) {
          if (i) joined += " + ";
          joined += emitExpr(segs[i], em);
        }
        std::string tgt = isCerr ? "System.err" : "System.out";
        if (endsEndl || joined.empty()) {
          if (joined.empty()) out.line(tgt + ".println();");
          else out.line(tgt + ".println(" + joined + ");");
        } else {
          out.line(tgt + ".print(" + joined + ");");
        }
        return true;
      }
      std::string es = emitExpr(s->expr, em);
      // Java rejects '(...)' as an expression statement; drop one outer
      // wrapper produced by the operand-parenthesizing emitter.
      if (es.size() >= 2 && es.front() == '(' && es.back() == ')') {
        int depth = 0;
        bool wrapsAll = true;
        for (size_t i = 0; i + 1 < es.size(); ++i) {
          if (es[i] == '(') ++depth;
          else if (es[i] == ')') --depth;
          if (depth == 0) { wrapsAll = false; break; }
        }
        if (wrapsAll) es = es.substr(1, es.size() - 2);
      }
      out.line(es + ";");
      return true;
    }
    case Stmt::K::If: {
      out.line("if (" + emitExpr(s->cond, em) + ") {");
      ++out.indent;
      emitStmt(s->thenS, em, out);
      --out.indent;
      out.line("}");
      if (s->elseS) {
        out.line("else {");
        ++out.indent;
        emitStmt(s->elseS, em, out);
        --out.indent;
        out.line("}");
      }
      return true;
    }
    case Stmt::K::While: {
      out.line("while (" + emitExpr(s->cond, em) + ") {");
      ++out.indent;
      emitStmt(s->body, em, out);
      --out.indent;
      out.line("}");
      return true;
    }
    case Stmt::K::Do: {
      out.line("do {");
      ++out.indent;
      emitStmt(s->body, em, out);
      --out.indent;
      out.line("} while (" + emitExpr(s->cond, em) + ");");
      return true;
    }
    case Stmt::K::For: {
      // Iterator-style loops (begin/end) are not portable; skip with a note.
      if (s->hasForInitV) {
        for (auto& d : s->forInitV.decls)
          if (d.init && d.init->k == Expr::K::Call && d.init->callee &&
              (d.init->callee->k == Expr::K::Member ||
               d.init->callee->k == Expr::K::Scope) &&
              (d.init->callee->name == "begin")) {
            em.ctx.warn(s->line,
                        "iterator-style for-loop not supported; use range-for");
            return false;
          }
      }
      std::string init;
      if (s->hasForInitV) {
        std::string t = tn(s->forInitV.type, em.ctx, s->line);
        for (auto& d : s->forInitV.decls) {
          if (!init.empty()) init += ", ";
          if (d.init)
            init += t + " " + sanitizeJavaName(d.name) + " = " +
                    initVal(d.init, s->forInitV.type, em);
          else
            init += t + " " + sanitizeJavaName(d.name);
        }
        if (s->forInitV.decls.size() == 1)
          em.ctx.locals.add(s->forInitV.decls[0].name, s->forInitV.type);
      } else if (s->forInitE) {
        init = emitExpr(s->forInitE, em);
      }
      std::string cond = s->forCond ? emitExpr(s->forCond, em) : "";
      std::string inc = s->forInc ? emitExpr(s->forInc, em) : "";
      out.line("for (" + init + "; " + cond + "; " + inc + ") {");
      ++out.indent;
      emitStmt(s->body, em, out);
      --out.indent;
      out.line("}");
      return true;
    }
    case Stmt::K::ForEach: {
      std::string t = (s->eachType && lastSegName(s->eachType->base) != "auto")
                          ? tn(s->eachType, em.ctx, s->line) + " "
                          : "var ";
      out.line("for (" + t + sanitizeJavaName(s->eachName) + " : " +
               emitExpr(s->eachColl, em) + ") {");
      ++out.indent;
      em.ctx.locals.push();
      em.ctx.locals.add(s->eachName, s->eachType);
      emitStmt(s->body, em, out);
      em.ctx.locals.pop();
      --out.indent;
      out.line("}");
      return true;
    }
    case Stmt::K::Switch: {
      out.line("switch (" + emitExpr(s->swCond, em) + ") {");
      ++out.indent;
      for (auto& cs : s->swCases) {
        if (cs.isDefault) out.line("default:");
        else out.line("case " + emitExpr(cs.value, em) + ":");
        em.ctx.locals.push();
        ++out.indent;
        for (auto& st : cs.stmts) emitStmt(st, em, out);
        --out.indent;
        em.ctx.locals.pop();
      }
      --out.indent;
      out.line("}");
      return true;
    }
    case Stmt::K::Case:
    case Stmt::K::Label: {
      if (s->k == Stmt::K::Label)
        em.ctx.warn(s->line, "goto label '" + s->label + "' dropped");
      emitStmt(s->labelBody ? s->labelBody : s->thenS, em, out);
      return true;
    }
    case Stmt::K::Break: out.line("break;"); return true;
    case Stmt::K::Continue: out.line("continue;"); return true;
    case Stmt::K::Return: {
      if (s->hasValue) {
        if (em.inMain) {
          em.ctx.warn(s->line, "value returned from main() dropped");
          out.line("return;");
        } else if (s->expr && s->expr->k == Expr::K::ArrayInit) {
          // C++ braced return value: 'return {};' / 'return {a, b};'
          if (s->expr->args.empty()) {
            std::string d = em.ctx.retType
                                ? defaultLocalInit(em.ctx.retType, s->line, em.ctx)
                                : std::string("null");
            em.ctx.warn(s->line, "'return {};' approximated as '" + d + "'");
            out.line("return " + d + ";");
          } else {
            out.line("return " + initVal(s->expr, em.ctx.retType, em) + ";");
          }
        } else {
          out.line("return " + emitExpr(s->expr, em) + ";");
        }
      } else {
        out.line("return;");
      }
      return true;
    }
    case Stmt::K::Throw: {
      const ExprPtr& x = s->expr;
      std::string txt;
      if (x->k == Expr::K::Call && x->callee) {
        std::string cls;
        if (x->callee->k == Expr::K::Scope || x->callee->k == Expr::K::Name)
          cls = x->callee->name;
        if (cls == "runtime_error" || cls == "exception")
          cls = "RuntimeException";
        else if (cls == "logic_error" || cls == "invalid_argument" ||
                 cls == "length_error" || cls == "domain_error" ||
                 cls == "out_of_range")
          cls = "IllegalArgumentException";
        else if (cls == "bad_cast") cls = "ClassCastException";
        std::string args;
        for (size_t i = 0; i < x->args.size(); ++i) {
          if (i) args += ", ";
          args += emitExpr(x->args[i], em);
        }
        txt = "throw new " + cls + "(" + args + ");";
      } else if (x->k == Expr::K::NewObj) {
        txt = "throw " + emitExpr(x, em) + ";";
      } else if (x->k == Expr::K::Name) {
        txt = "throw " + x->name + ";";  // rethrow / exception variable
      } else {
        em.ctx.warn(s->line, "throw expression approximated as RuntimeException");
        txt = "throw new RuntimeException(String.valueOf(" + emitExpr(x, em) + "));";
      }
      out.line(txt);
      return true;
    }
    case Stmt::K::Try: {
      out.line("try {");
      ++out.indent;
      emitBlock(s->tryBlock, em, out);
      --out.indent;
      out.line("}");
      for (auto& cc : s->catches) {
        std::string t = tn(cc.type, em.ctx, s->line);
        if (!cc.type || lastSegName(cc.type->base) == "...") {
          out.line("catch (Throwable " + cc.name + ") {");
        } else {
          out.line("catch (" + t + " " + cc.name + ") {");
        }
        ++out.indent;
        em.ctx.locals.push();
        em.ctx.locals.add(cc.name, cc.type);
        emitBlock(cc.body, em, out);
        em.ctx.locals.pop();
        --out.indent;
        out.line("}");
      }
      if (s->finallyBlock) {
        out.line("finally {");
        ++out.indent;
        emitBlock(s->finallyBlock, em, out);
        --out.indent;
        out.line("}");
      }
      return true;
    }
    case Stmt::K::Empty: {
      out.line(";");
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Method / field / class / enum emitters
// ---------------------------------------------------------------------------
void emitMethod(const Method& m, E& em, Out& out, bool iface) {
  if (m.isDtor) {
    em.ctx.warn(m.line, "destructor '" + m.name + "' skipped (no Java equivalent)");
    return;
  }
  // Track the enclosing return type so 'return {};' can be approximated.
  TypePtr savedRetType = em.ctx.retType;
  em.ctx.retType = m.ret;
  struct RetRestore {
    GenCtx& c;
    TypePtr t;
    ~RetRestore() { c.retType = t; }
  } retRestore{em.ctx, savedRetType};
  bool isCtor = m.isCtor;
  if (iface && isCtor) {
    em.ctx.warn(m.line, "constructor '" + m.name + "' skipped inside interface");
    return;
  }
  bool pure = hasMod(m.mods, "pure");
  bool isStatic = hasMod(m.mods, "static");

  std::string head;
  if (!m.typeParams.empty())
    head += "<" + joinStr(m.typeParams, ", ") + "> ";
  if (iface) {
    if (isStatic) head += "static ";
    // interface methods are implicitly public abstract
  } else {
    if (hasMod(m.mods, "private")) head += "private ";
    else if (hasMod(m.mods, "protected")) head += "protected ";
    else head += "public ";
    if (isStatic) head += "static ";
    else if (pure && !isCtor) head += "abstract ";
    else if (hasMod(m.mods, "final")) head += "final ";
  }
  if (!isCtor) {
    std::string r = tn(m.ret, em.ctx, m.line);
    if (r == "var") {
      em.ctx.warn(m.line, "auto return type approximated to Object");
      r = "Object";
    }
    head += r + " ";
  }
  head += sanitizeJavaName(m.name) + "(";

  size_t pi = 0;
  for (auto& p : m.params) {
    if (em.inMain && m.name == "main") {
      if (p.name == "argc") continue;
      if (p.name == "argv") {
        if (pi) head += ", ";
        head += "String[] argv";
        ++pi;
        continue;
      }
    }
    if (pi) head += ", ";
    if (p.def)
      em.ctx.warn(m.line, "default argument of '" + p.name + "' dropped");
    if (p.varargs)
      head += tn(p.type, em.ctx, m.line) + "... " + sanitizeJavaName(p.name);
    else
      head += tn(p.type, em.ctx, m.line) + " " + sanitizeJavaName(p.name);
    ++pi;
  }
  head += ")";

  em.ctx.locals.push();
  for (auto& p : m.params) em.ctx.locals.add(p.name, p.type);

  if (iface) {
    out.line(head + ";");
    em.ctx.locals.pop();
    return;
  }
  if (m.body) {
    out.line(head + " {");
    ++out.indent;
    // constructor initializer list -> assignments at body top
    for (auto& il : m.initList) {
      const std::string& fld = il.first;
      TypePtr ft = em.ctx.curClass.empty()
                       ? nullptr
                       : lookupField(em.ctx.syms, em.ctx.curClass, fld);
      if (!ft) {
        em.ctx.warn(m.line, "ctor init member '" + fld + "' not a field; skipped");
        continue;
      }
      std::string v = il.second ? emitExpr(il.second, em)
                                : defaultLocalInit(ft, m.line, em.ctx);
      out.line("this." + sanitizeJavaName(fld) + " = " + v + ";");
    }
    emitBlock(m.body, em, out);
    --out.indent;
    out.line("}");
  } else if (pure) {
    out.line(head + ";");
  } else {
    em.ctx.warn(m.line, "declaration of '" + m.name + "' has no body; stub generated");
    out.line(head + " { }");
  }
  em.ctx.locals.pop();
}

void emitField(const Field& f, E& em, Out& out) {
  std::string acc = "public";
  if (hasMod(f.mods, "private")) acc = "private";
  else if (hasMod(f.mods, "protected")) acc = "protected";
  bool isStatic = hasMod(f.mods, "static");
  bool isConst = hasMod(f.mods, "const") || hasMod(f.mods, "constexpr");
  std::string head = acc + " ";
  if (isStatic) head += isConst ? "static final " : "static ";
  else if (isConst) head += "final ";
  head += tn(f.type, em.ctx, f.line) + " " + sanitizeJavaName(f.name);
  if (f.init) {
    head += " = " + initVal(f.init, f.type, em);
  } else if (isStatic && !isConst) {
    std::string d = defaultLocalInit(f.type, f.line, em.ctx);
    if (!d.empty()) head += " = " + d;
  }
  out.line(head + ";");
}

// Pre-pass: which classes translate to Java interfaces?
std::set<std::string> collectInterfaces(const Program& p) {
  std::set<std::string> res;
  for (auto& c : p.classes) {
    if (c->isInterface) { res.insert(c->name); continue; }
    bool hasData = !c->fields.empty();
    bool hasCtorImpl = false;
    size_t methodCount = 0, pureCount = 0;
    for (auto& m : c->methods) {
      if (m.isDtor) continue;
      if (m.isCtor) { hasCtorImpl = true; continue; }
      ++methodCount;
      if (hasMod(m.mods, "pure")) ++pureCount;
    }
    if (!hasData && !hasCtorImpl && methodCount > 0 && pureCount == methodCount)
      res.insert(c->name);
  }
  return res;
}

void emitClass(const ClassDecl& c, E& em, Out& out,
               const std::set<std::string>& ifaces) {
  bool iface = ifaces.count(c.name) != 0;
  em.ctx.curClass = c.name;
  // Inside a namespace body, bare names of that namespace's globals/free
  // functions must be qualified with the namespace holder class.
  std::string savedNs = em.nsHolder;
  if (!c.ns.empty()) em.nsHolder = c.ns.back();

  std::string head = (c.isStruct ? "class " : iface ? "interface " : "class ") +
                     c.name;
  if (!c.typeParams.empty())
    head += "<" + joinStr(c.typeParams, ", ") + ">";

  if (!c.bases.empty()) {
    std::string ext, imp;
    for (auto& b : c.bases) {
      const std::string& bn = b.type ? b.type->base : "";
      if (bn.find("std::") == 0) continue;  // std exception bases are folded away
      std::string seg = lastSegName(bn);
      if (ifaces.count(seg)) {
        if (!imp.empty()) imp += ", ";
        imp += seg;
      } else if (ext.empty()) {
        ext = seg;
      } else {
        if (!imp.empty()) imp += ", ";
        imp += seg;
        em.ctx.warn(c.line, "multiple inheritance approximated: '" + seg + "' -> implements");
      }
    }
    if (!ext.empty()) head += " extends " + ext;
    if (!imp.empty()) head += " implements " + imp;
  }

  bool hasPure = false;
  for (auto& m : c.methods)
    if (!m.isDtor && hasMod(m.mods, "pure")) { hasPure = true; break; }
  if (!iface && hasPure) head = "abstract " + head;

  out.line("");
  out.open(head);
  if (iface) {
    for (auto& f : c.fields)
      em.ctx.warn(f.line, "interface field '" + f.name + "' dropped");
  } else {
    for (auto& f : c.fields) emitField(f, em, out);
  }
  for (auto& m : c.methods) emitMethod(m, em, out, iface);
  out.closePlain();
  em.ctx.curClass = "";
  em.nsHolder = savedNs;
}

void emitEnum(const EnumDecl& e, E& em, Out& out) {
  out.line("");
  out.line("enum " + e.name + " {");
  ++out.indent;
  std::vector<std::string> vals;
  bool savedInEnum = em.inEnumBody;
  em.inEnumBody = true;  // constants are emitted bare inside their own enum
  for (auto& v : e.values) {
    std::string nm = v;
    size_t eq = v.find('=');
    if (eq != std::string::npos) {
      em.ctx.warn(e.line, "explicit enum constants dropped (found in '" + v + "')");
      nm = v.substr(0, eq);
    }
    vals.push_back(sanitizeJavaName(nm));
  }
  em.inEnumBody = savedInEnum;
  std::string txt = joinStr(vals, ", ");
  if (!txt.empty()) out.line(txt);
  --out.indent;
  out.line("}");
}

// ---------------------------------------------------------------------------
// Top-level variables -> 'public static' fields of the holder class.
// 'static constexpr' / 'const' become 'static final'; plain globals become
// 'static' (Java has no file-scope storage).
// ---------------------------------------------------------------------------
void emitGlobalField(const Field& f, E& em, Out& out) {
  bool isConst = hasMod(f.mods, "const") || hasMod(f.mods, "constexpr") ||
                 hasMod(f.mods, "constinit");
  bool isExtern = hasMod(f.mods, "extern");
  std::string head = "public static ";
  // A 'final' field must be initialized where it is declared.
  if (isConst && f.init) head += "final ";
  head += tn(f.type, em.ctx, f.line) + " " + sanitizeJavaName(f.name);
  if (f.init) {
    head += " = " + initVal(f.init, f.type, em);
  } else if (!isExtern) {
    std::string d = defaultLocalInit(f.type, f.line, em.ctx);
    if (!d.empty()) head += " = " + d;
  }
  out.line(head + ";");
}

// ---------------------------------------------------------------------------
// Top-level functions -> 'public static' methods of the holder class, so
// bare-name calls between them (and to the classes above) keep resolving.
// ---------------------------------------------------------------------------
void emitStaticMethod(const Method& m, E& em, Out& out) {
  Method copy = m;
  bool hasPublic = false, hasStatic = false;
  for (auto& md : copy.mods) {
    if (md == "public") hasPublic = true;
    if (md == "static") hasStatic = true;
  }
  if (!hasStatic) copy.mods.insert(copy.mods.begin(), "static");
  if (!hasPublic) copy.mods.insert(copy.mods.begin(), "public");
  emitMethod(copy, em, out, false);
}

// ---------------------------------------------------------------------------
// main() wrapper: C++ free main -> 'public static void main(String[] argv)'
// inside the holder class, skipping argc and rebuilding the argument vector
// in a Java-idiomatic way.
// ---------------------------------------------------------------------------
void emitMainBody(const Method& m, E& em, Out& out) {
  em.inMain = true;
  em.ctx.retType = nullptr;  // main returns void
  out.line("public static void main(String[] argv) {");
  ++out.indent;
  em.ctx.locals.push();
  for (auto& p : m.params)
    if (p.name == "argv") em.ctx.locals.add("argv", p.type);
  if (m.body) emitBlock(m.body, em, out);
  else em.ctx.warn(m.line, "main() without a body");
  em.ctx.locals.pop();
  --out.indent;
  out.line("}");
  em.inMain = false;
  em.ctx.retType = nullptr;
}

bool textNeedsJavaUtil(const std::string& s) {
  static const char* marks[] = {
      "ArrayList", "HashMap", "HashSet", "LinkedList", "Optional",
      "Arrays.asList", "List<", "Set<", "Map<", "Iterator", "Queue<"};
  for (auto* mk : marks)
    if (s.find(mk) != std::string::npos) return true;
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
std::string generateC2J(const std::shared_ptr<Program>& p,
                        std::vector<Diag>& diags) {
  GenCtx ctx(diags);
  ctx.prog = p.get();
  ctx.syms = buildSymbols(*p, diags);
  E em{ctx, false};

  // Enum constants: Java requires 'Enum.CONST' outside the enum body, and an
  // enum has no numeric cast ('.ordinal()' instead).
  for (auto& ep : p->enums) {
    const EnumDecl& e = *ep;
    ctx.enumNames.insert(e.name);
    for (auto& v : e.values) {
      std::string nm = v;
      size_t eq = v.find('=');
      if (eq != std::string::npos) nm = v.substr(0, eq);
      ctx.enumConstOwner[sanitizeJavaName(nm)] = e.name;
    }
  }
  // Namespace holder class -> member names, for qualifying bare references.
  for (auto& ns : p->namespaces) {
    if (ns.parts.empty()) continue;
    std::string clsName = ns.parts.back();
    std::set<std::string>& members = ctx.nsMembers[clsName];
    for (auto& g : ns.globals) members.insert(g.name);
    for (auto& f : ns.freeFuncs) members.insert(f->name);
  }

  const std::set<std::string> ifaces = collectInterfaces(*p);

  Out body;
  for (auto& c : p->classes) emitClass(*c, em, body, ifaces);
  for (auto& e : p->enums) emitEnum(*e, em, body);

  // Named namespace bodies become holder classes named after the namespace,
  // so 'YXStyle::CONST' references keep resolving ('YXStyle.CONST').
  // Not 'public': everything lands in a single Main.java, and javac only
  // allows one public top-level class per file.
  for (auto& ns : p->namespaces) {
    if (ns.globals.empty() && ns.freeFuncs.empty()) continue;
    std::string clsName = ns.parts.empty() ? "LangBridgeNs" : ns.parts.back();
    body.line("");
    body.line("class " + clsName + " {");
    ++body.indent;
    std::string savedNs = em.nsHolder;
    em.nsHolder = clsName;
    for (auto& g : ns.globals) emitGlobalField(g, em, body);
    for (auto& f : ns.freeFuncs) emitStaticMethod(*f, em, body);
    em.nsHolder = savedNs;
    --body.indent;
    body.line("}");
  }

  bool hasMain = false;
  for (auto& f : p->freeFuncs)
    if (f->name == "main") { hasMain = true; break; }

  // Top-level functions and variables have no Java file-scope equivalent:
  // fold them into the 'Main' holder class as static members (main() stays
  // the entry point). This keeps the old single-file output contract.
  if (hasMain || !p->freeFuncs.empty() || !p->globals.empty()) {
    body.line("");
    body.line("public class Main {");
    ++body.indent;
    for (auto& g : p->globals) emitGlobalField(g, em, body);
    if (hasMain) {
      for (auto& f : p->freeFuncs)
        if (f->name == "main") emitMainBody(*f, em, body);
    }
    for (auto& f : p->freeFuncs) {
      if (f->name == "main") continue;
      emitStaticMethod(*f, em, body);
    }
    --body.indent;
    body.line("}");
  }

  Out o;
  o.line("// Generated by LangBridge (C++ -> Java). Best-effort translation;");
  o.line("// known approximations are documented in docs/README.md.");
  if (!p->packageParts.empty())
    o.line("package " + joinStr(p->packageParts, ".") + ";");
  if (textNeedsJavaUtil(body.str()))
    o.line("import java.util.*;");
  o.line("");
  o.raw(body.str());
  return o.str();
}

}  // namespace bridge