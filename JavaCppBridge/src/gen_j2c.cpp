// gen_j2c.cpp - Java -> C++17 code generator (T4).
// Translates the shared AST (Java flavour) into an independent C++17
// translation unit with a small stdbridge runtime helper namespace.
#include "bridge.hpp"
#include <algorithm>
#include <set>

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

bool isStrT(const TypePtr& t) {
  if (!t) return false;
  std::string b = lastSegName(t->base);
  return b == "String" || b == "StringBuilder" || b == "StringBuffer" ||
         b == "CharSequence";
}
bool listLikeT(const TypePtr& t) {
  if (!t || t->dims > 0) return false;
  static const std::set<std::string> n = {"List","ArrayList","LinkedList",
      "Collection","Iterable","Queue","Deque","ArrayDeque","Stack",
      "PriorityQueue"};
  return n.count(lastSegName(t->base)) != 0;
}
bool mapLikeT(const TypePtr& t) {
  if (!t || t->dims > 0) return false;
  static const std::set<std::string> n = {"Map","HashMap","TreeMap",
      "LinkedHashMap","SortedMap","ConcurrentHashMap","NavigableMap"};
  return n.count(lastSegName(t->base)) != 0;
}
bool setLikeT(const TypePtr& t) {
  if (!t || t->dims > 0) return false;
  static const std::set<std::string> n = {"Set","HashSet","TreeSet",
      "SortedSet","LinkedHashSet","NavigableSet"};
  return n.count(lastSegName(t->base)) != 0;
}
bool excLike(const std::string& b) {
  static const std::set<std::string> n = {"Exception","RuntimeException",
      "IllegalArgumentException","IllegalStateException","NullPointerException",
      "IndexOutOfBoundsException","ArrayIndexOutOfBoundsException",
      "UnsupportedOperationException","Throwable","Error"};
  return n.count(b) != 0;
}
bool excLikeT(const TypePtr& t) {
  return t && excLike(lastSegName(t->base));
}
bool staticCls(const std::string& n) {
  return n == "Math" || n == "Integer" || n == "Long" || n == "Double" ||
         n == "Float" || n == "Boolean" || n == "String" || n == "System" ||
         n == "Objects" || n == "Collections" || n == "Arrays";
}

bool isInterfaceName(const std::string& n, const GenCtx& ctx) {
  const ClassInfo* ci = findClass(ctx.syms, n);
  return ci && ci->isInterface;
}
bool isEnumName(const std::string& n, const GenCtx& ctx) {
  if (!ctx.prog) return false;
  for (auto& e : ctx.prog->enums) if (e->name == n) return true;
  return false;
}

// ---------------------------------------------------------------------------
// Type mapping (Java source type -> C++ type; interface types become
// std::shared_ptr<IFace> so polymorphic references compile).
// ---------------------------------------------------------------------------
std::string mapTypeImpl(const Type& t, GenCtx& ctx) {
  if (std::find(ctx.typeParams.begin(), ctx.typeParams.end(), t.base) !=
      ctx.typeParams.end()) {
    std::string out = t.base;
    for (int d = 0; d < t.dims; ++d) out = "std::vector<" + out + ">";
    return out;
  }
  std::string b = lastSegName(t.base);
  if (b == "Scanner") {
    // java.util.Scanner lives on std::cin for converted CLI code.
    for (int d = 0; d < t.dims; ++d) { (void)d; }
    if (t.dims > 0) return "std::vector<std::istream&>";
    return "std::istream&";
  }
  const ClassInfo* ci = findClass(ctx.syms, b);
  if (ci && ci->isInterface && !t.isPtr && !t.isRef) {
    std::string inner = b;
    if (!t.args.empty()) {
      inner += "<";
      for (size_t i = 0; i < t.args.size(); ++i) {
        if (i) inner += ", ";
        inner += mapTypeImpl(t.args[i], ctx);
      }
      inner += ">";
    }
    if (t.isConst) inner = "const " + inner;
    if (t.isUnsigned) inner = "unsigned " + inner;
    for (int d = 0; d < t.dims; ++d) inner = "std::vector<" + inner + ">";
    return "std::shared_ptr<" + inner + ">";
  }
  return mapJ2C(t, ctx.typeParams);
}
std::string mapType(const TypePtr& t, GenCtx& ctx) {
  if (!t) return "void";
  return mapTypeImpl(*t, ctx);
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
std::string emitExpr(const ExprPtr& e, GenCtx& ctx);
void emitStmt(const StmtPtr& st, GenCtx& ctx, Out& o);

// ---------------------------------------------------------------------------
// String literal render
// ---------------------------------------------------------------------------
std::string strLit(const std::string& inner) {
  return "\"" + litEscape(inner) + "\"";
}

// ---------------------------------------------------------------------------
// Expression emission helpers
// ---------------------------------------------------------------------------
bool compoundExpr(const ExprPtr& e) {
  if (!e) return false;
  switch (e->k) {
    case Expr::K::Binary: case Expr::K::Unary: case Expr::K::Assign:
    case Expr::K::IncDec: case Expr::K::Cast: case Expr::K::Ternary:
    case Expr::K::Paren:
      return true;
    default:
      return false;
  }
}
std::string baseExpr(const ExprPtr& e, GenCtx& ctx) {
  std::string s = emitExpr(e, ctx);
  return compoundExpr(e) ? "(" + s + ")" : s;
}

// Access operator: interfaces are held via shared_ptr -> use '->'.
std::string acOp(const ExprPtr& obj, GenCtx& ctx) {
  TypePtr t = inferJavaType(obj, ctx);
  if (t && isInterfaceName(lastSegName(t->base), ctx)) return "->";
  return ".";
}

// First usable base class name (for super(...) and super:: calls).
std::string firstBaseName(GenCtx& ctx) {
  if (ctx.curClass.empty()) return "";
  const ClassInfo* ci = findClass(ctx.syms, ctx.curClass);
  if (!ci) return "";
  for (auto& b : ci->bases) {
    if (!b.type) continue;
    std::string bn = lastSegName(b.type->base);
    if (isInterfaceName(bn, ctx) || isEnumName(bn, ctx)) continue;
    if (excLike(bn)) return "std::runtime_error";
    return bn;
  }
  return "";
}

std::string emitConcatPart(const ExprPtr& e, GenCtx& ctx) {
  if (!e) return "std::string()";
  switch (e->k) {
    case Expr::K::StrLit: return strLit(e->sval);
    case Expr::K::Null: return "std::string(\"null\")";
    case Expr::K::BoolLit:
      return e->bval ? "std::string(\"true\")" : "std::string(\"false\")";
    default: break;
  }
  TypePtr t = inferJavaType(e, ctx);
  if (isStrT(t)) return emitExpr(e, ctx);
  if (t && lastSegName(t->base) == "char")
    return "std::string(1, " + emitExpr(e, ctx) + ")";
  if (t && lastSegName(t->base) == "boolean")
    return "(" + emitExpr(e, ctx) + " ? \"true\" : \"false\")";
  return "std::to_string(" + emitExpr(e, ctx) + ")";
}

std::string emitConcat(const ExprPtr& e, GenCtx& ctx) {
  if (e && e->k == Expr::K::Binary && e->op == "+")
    return "(" + emitConcatPart(e->lhs, ctx) + " + " +
           emitConcatPart(e->rhs, ctx) + ")";
  return emitConcatPart(e, ctx);
}

std::string joinArgs(const std::vector<ExprPtr>& args, GenCtx& ctx,
                     const Method* m = nullptr) {
  std::string out;
  for (size_t i = 0; i < args.size(); ++i) {
    if (i) out += ", ";
    std::string a = emitExpr(args[i], ctx);
    if (m && i < m->params.size() && m->params[i].type &&
        lastSegName(m->params[i].type->base) == "Object" &&
        !(m->params[i].type->dims > 0)) {
      out += "std::any(" + a + ")";
    } else {
      out += a;
    }
  }
  return out;
}

// Math / Integer / System static rewrites.
std::string emitStaticCall(const std::string& cls, const std::string& name,
                           const std::vector<ExprPtr>& args, GenCtx& ctx) {
  if (cls == "Math") {
    if (name == "max" || name == "min")
      return "std::" + name + "(" + joinArgs(args, ctx) + ")";
    if (name == "abs" || name == "pow" || name == "sqrt" || name == "floor" ||
        name == "ceil" || name == "round")
      return "std::" + name + "(" + joinArgs(args, ctx) + ")";
    if (name == "random")
      return "(static_cast<double>(std::rand()) / RAND_MAX)";
    ctx.warn(0, "Math." + name + " unsupported; emitted as-is");
    return "(" + joinArgs(args, ctx) + ") /* Math." + name + " */";
  }
  if (cls == "Integer" || cls == "Long" || cls == "Float" || cls == "Double") {
    if (name == "parseInt") return "std::stoi(" + joinArgs(args, ctx) + ")";
    if (name == "parseLong") return "std::stoll(" + joinArgs(args, ctx) + ")";
    if (name == "parseDouble") return "std::stod(" + joinArgs(args, ctx) + ")";
    if (name == "parseFloat") return "std::stof(" + joinArgs(args, ctx) + ")";
    if (name == "valueOf") return "std::stoi(" + joinArgs(args, ctx) + ")";
    if (name == "toString") return "std::to_string(" + joinArgs(args, ctx) + ")";
    return "(" + joinArgs(args, ctx) + ") /* " + cls + "." + name + " */";
  }
  if (cls == "Boolean") {
    if (name == "parseBoolean") {
      std::string a = args.empty() ? "s" : emitExpr(args[0], ctx);
      return "(" + a + " == \"true\" || " + a + " == \"True\")";
    }
  }
  if (cls == "String") {
    if (name == "valueOf") {
      if (args.empty()) return "std::string()";
      TypePtr t = inferJavaType(args[0], ctx);
      if (isStrT(t)) return emitExpr(args[0], ctx);
      if (t && lastSegName(t->base) == "char")
        return "std::string(1, " + emitExpr(args[0], ctx) + ")";
      if (t && lastSegName(t->base) == "boolean")
        return "(" + emitExpr(args[0], ctx) + " ? \"true\" : \"false\")";
      return "std::to_string(" + emitExpr(args[0], ctx) + ")";
    }
    if (name == "format") {
      ctx.warn(0, "String.format unsupported; emitted without formatting");
      return joinArgs(args, ctx) + " /* String.format */";
    }
    if (name == "join") {
      ctx.warn(0, "String.join unsupported");
      return "std::string() /* String.join */";
    }
  }
  if (cls == "System") {
    if (name == "currentTimeMillis")
      return "static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>"
             "(std::chrono::system_clock::now().time_since_epoch()).count())";
    if (name == "nanoTime")
      return "static_cast<long long>(std::chrono::steady_clock::now()"
             ".time_since_epoch().count())";
    if (name == "gc") return "(void)0";
    if (name == "arraycopy") {
      ctx.warn(0, "System.arraycopy approximated per element");
      if (args.size() >= 5) {
        std::string cnt = emitExpr(args[4], ctx);
        std::string src = emitExpr(args[0], ctx);
        std::string dst = emitExpr(args[2], ctx);
        return "(std::copy(" + src + ".begin() + " + emitExpr(args[1], ctx) +
               ", " + src + ".begin() + " + emitExpr(args[1], ctx) + " + " + cnt +
               ", " + dst + ".begin() + " + emitExpr(args[3], ctx) + "), void())";
      }
      return "void()";
    }
  }
  if (cls == "Objects") {
    if (name == "requireNonNull" && !args.empty())
      return emitExpr(args[0], ctx);
    if (name == "equals" && args.size() == 2)
      return "(" + emitExpr(args[0], ctx) + " == " + emitExpr(args[1], ctx) + ")";
    if (name == "hashCode" && !args.empty())
      return emitExpr(args[0], ctx) + " /* Objects.hashCode */";
  }
  if ((cls == "Collections" || cls == "Arrays") && !args.empty()) {
    ctx.warn(0, cls + "." + name + " approximated (raw argument list)");
    return "(" + joinArgs(args, ctx) + ") /* " + cls + "." + name + " */";
  }
  return "";
}

// Rewrite a Java String method call onto std::string operations.
std::string emitStringCall(const std::string& name, const std::string& recv,
                           const std::vector<ExprPtr>& args, GenCtx& ctx) {
  auto a = [&](size_t i) {
    return (i < args.size() && args[i]) ? emitExpr(args[i], ctx)
                                        : std::string();
  };
  if (name == "length") return recv + ".size()";
  if (name == "isEmpty") return recv + ".empty()";
  if (name == "charAt") return recv + ".at(" + a(0) + ")";
  if (name == "codePointAt")
    return "static_cast<int>(" + recv + ".at(" + a(0) + "))";
  if (name == "substring") {
    if (args.size() >= 2)
      return recv + ".substr(" + a(0) + ", " + a(1) + " - " + a(0) + ")";
    return recv + ".substr(" + a(0) + ")";
  }
  if (name == "indexOf") return recv + ".find(" + a(0) + ")";
  if (name == "lastIndexOf") return recv + ".rfind(" + a(0) + ")";
  if (name == "contains")
    return recv + ".find(" + a(0) + ") != std::string::npos";
  if (name == "startsWith")
    return "stdbridge::startsWith(" + recv + ", " + a(0) + ")";
  if (name == "endsWith")
    return "stdbridge::endsWith(" + recv + ", " + a(0) + ")";
  if (name == "trim" || name == "strip")
    return "stdbridge::trim(" + recv + ")";
  if (name == "toLowerCase") return "stdbridge::toLowerCase(" + recv + ")";
  if (name == "toUpperCase") return "stdbridge::toUpperCase(" + recv + ")";
  if (name == "equals") return "(" + recv + " == " + a(0) + ")";
  if (name == "equalsIgnoreCase")
    return "stdbridge::equalsIgnoreCase(" + recv + ", " + a(0) + ")";
  if (name == "compareTo") return recv + ".compare(" + a(0) + ")";
  if (name == "compareToIgnoreCase")
    return "stdbridge::toLowerCase(" + recv + ").compare(" +
           "stdbridge::toLowerCase(" + a(0) + "))";
  if (name == "replace")
    return "stdbridge::replaceAllS(" + recv + ", " + a(0) + ", " + a(1) + ")";
  if (name == "replaceAll" || name == "replaceFirst") {
    ctx.warn(0, "String." + name + " uses literal replacement (no regex)");
    return "stdbridge::replaceAllS(" + recv + ", " + a(0) + ", " + a(1) + ")";
  }
  if (name == "repeat") return "stdbridge::repeat(" + recv + ", " + a(0) + ")";
  if (name == "split")
    return "stdbridge::split(" + recv + ", " + a(0) + ")";
  if (name == "concat") return "(" + recv + " + " + a(0) + ")";
  if (name == "toCharArray")
    return "std::vector<char>(" + recv + ".begin(), " + recv + ".end())";
  if (name == "toString" || name == "intern") return recv;
  if (name == "matches") {
    ctx.warn(0, "String.matches unsupported (no regex)");
    return "(false)";
  }
  if (name == "getBytes") {
    ctx.warn(0, "String.getBytes approximated");
    return "std::vector<unsigned char>(" + recv + ".begin(), " +
           recv + ".end())";
  }
  ctx.warn(0, "String." + name + " unsupported; emitted as-is");
  return recv + "." + name + "(" + joinArgs(args, ctx) + ")";
}

// Rewrite a container-typed method call.
std::string emitContainerCall(const TypePtr& rt, const std::string& name,
                              const std::string& recv,
                              const std::vector<ExprPtr>& args, GenCtx& ctx) {
  auto a = [&](size_t i) {
    return (i < args.size() && args[i]) ? emitExpr(args[i], ctx)
                                        : std::string();
  };
  if (name == "size") return recv + ".size()";
  if (name == "isEmpty") return recv + ".empty()";
  if (name == "clear") return recv + ".clear()";
  if (listLikeT(rt)) {
    if (name == "get") return recv + "[" + a(0) + "]";
    if (name == "set") return "(" + recv + "[" + a(0) + "] = " + a(1) + ")";
    if (name == "add") {
      if (args.size() >= 2)
        return recv + ".insert(" + recv + ".begin() + " + a(0) + ", " + a(1) + ")";
      return recv + ".push_back(" + a(0) + ")";
    }
    if (name == "remove") {
      TypePtr at = inferJavaType(args[0], ctx);
      bool numeric = at && (lastSegName(at->base) == "int" ||
                            lastSegName(at->base) == "long" ||
                            lastSegName(at->base) == "Integer");
      if (numeric || !at)
        return recv + ".erase(" + recv + ".begin() + " + a(0) + ")";
      return recv + ".erase(std::find(" + recv + ".begin(), " + recv +
             ".end(), " + a(0) + "))";
    }
    if (name == "addAll")
      return recv + ".insert(" + recv + ".end(), " + a(0) + ".begin(), " +
             a(0) + ".end())";
    if (name == "contains")
      return "stdbridge::containsVec(" + recv + ", " + a(0) + ")";
    if (name == "indexOf" || name == "lastIndexOf")
      return "stdbridge::indexOfVec(" + recv + ", " + a(0) + ")";
    if (name == "toArray") {
      std::string el = "auto";
      if (rt && !rt->args.empty()) el = mapTypeImpl(rt->args[0], ctx);
      return "std::vector<" + el + ">(" + recv + ".begin(), " +
             recv + ".end())";
    }
    if (name == "sort") {
      ctx.warn(0, "List.sort with comparator unsupported");
      return "(std::sort(" + recv + ".begin(), " + recv + ".end()), " +
             recv + ")";
    }
    if (name == "forEach") {
      ctx.warn(0, "List.forEach approximated");
      return "(" + recv + " /* forEach */)";
    }
  }
  if (mapLikeT(rt)) {
    if (name == "put") return "(" + recv + "[" + a(0) + "] = " + a(1) + ")";
    if (name == "get") return recv + ".at(" + a(0) + ")";
    if (name == "getOrDefault")
      return "(" + recv + ".count(" + a(0) + ") ? " + recv + ".at(" + a(0) +
             ") : " + a(1) + ")";
    if (name == "containsKey")
      return "stdbridge::containsKey(" + recv + ", " + a(0) + ")";
    if (name == "containsValue")
      return "(std::find_if(" + recv + ".begin(), " + recv +
             ".end(), [&](const auto& p) { return p.second == " + a(0) +
             "; }) != " + recv + ".end())";
    if (name == "remove") return recv + ".erase(" + a(0) + ")";
    if (name == "keySet" || name == "values" || name == "entrySet")
      return recv;
    if (name == "putIfAbsent") {
      ctx.warn(0, "Map.putIfAbsent approximated");
      return "(" + recv + ".emplace(" + a(0) + ", " + a(1) + ").first->second)";
    }
    if (name == "merge" || name == "computeIfAbsent") {
      ctx.warn(0, "Map." + name + " unsupported");
      return recv + " /* " + name + " */";
    }
  }
  if (setLikeT(rt)) {
    if (name == "add") return recv + ".insert(" + a(0) + ")";
    if (name == "contains") return "(" + recv + ".count(" + a(0) + ") != 0)";
    if (name == "remove") return "(" + recv + ".erase(" + a(0) + ") != 0)";
    if (name == "addAll")
      return recv + ".insert(" + a(0) + ".begin(), " + a(0) + ".end())";
    if (name == "containsAll") {
      ctx.warn(0, "Set.containsAll approximated");
      return "(false)";
    }
  }
  return recv + "." + name + "(" + joinArgs(args, ctx) + ")";
}

// Scanner rewrites (std::cin based).
std::string emitScannerCall(const std::string& name, const std::string& recv,
                            std::vector<ExprPtr>& args, GenCtx& ctx) {
  if (name == "nextLine")
    return "([](std::istream& is) { std::string l; "
           "std::getline(is, l); return l; })(std::cin)";
  if (name == "next")
    return "([](std::istream& is) { std::string l; "
           "std::getline(is, l); return l; })(std::cin)";
  if (name == "nextInt")
    return "([](std::istream& is) { int x; is >> x; return x; })(std::cin)";
  if (name == "nextDouble")
    return "([](std::istream& is) { double x; is >> x; return x; })(std::cin)";
  if (name == "nextLong")
    return "([](std::istream& is) { long long x; is >> x; return x; })(std::cin)";
  if (name == "hasNextLine" || name == "hasNext") return "(std::cin.good())";
  if (name == "close") return "(void)" + recv;
  ctx.warn(0, "Scanner." + name + " unsupported");
  return "(void)" + recv;
}

// ---------------------------------------------------------------------------
// Call emission (with method-call rewrites)
// ---------------------------------------------------------------------------
std::string emitCall(const ExprPtr& e, GenCtx& ctx) {
  const Expr& c = *e->callee;
  if (c.k == Expr::K::Name) {
    std::string n = sanitizeCppName(c.name);
    return n + "(" + joinArgs(e->args, ctx) + ")";
  }
  if (c.k != Expr::K::Member || !c.obj) return "void()";
  std::string name = c.name;
  ExprPtr obj = c.obj;

  // --- static utility classes (Math, Integer, System, ...) ---
  if (obj->k == Expr::K::Name && staticCls(obj->name)) {
    std::string r = emitStaticCall(obj->name, name, e->args, ctx);
    if (!r.empty()) return r;
  }

  // --- static method on a user class: Counter.add(...) -> Counter::add(...)
  if (obj->k == Expr::K::Name && !ctx.curClass.empty() &&
      ctx.syms.count(obj->name) != 0) {
    const ClassInfo* ci = findClass(ctx.syms, obj->name);
    const Method* m = nullptr;
    if (ci) {
      for (auto& ms : ci->allMethods) {
        if (ms.name == name && ms.params.size() == e->args.size() &&
            hasMod(ms.m->mods, "static")) {
          m = ms.m;
          break;
        }
      }
    }
    if (m) {
      return sanitizeCppName(obj->name) + "::" + sanitizeCppName(name) +
             "(" + joinArgs(e->args, ctx, m) + ")";
    }
  }

  // --- this / super receivers ---
  if (obj->k == Expr::K::This)
    return "this->" + sanitizeCppName(name) + "(" + joinArgs(e->args, ctx) + ")";
  if (obj->k == Expr::K::Name && obj->name == "super") {
    std::string base = firstBaseName(ctx);
    if (base.empty()) base = "this->";
    else base += "::";
    return base + sanitizeCppName(name) + "(" + joinArgs(e->args, ctx) + ")";
  }

  // --- receiver-typed rewrites ---
  TypePtr rt = inferJavaType(obj, ctx);
  std::string recv = baseExpr(obj, ctx);
  std::string op = acOp(obj, ctx);
  if (rt) {
    std::string rb = lastSegName(rt->base);
    if (isStrT(rt)) return emitStringCall(name, recv, e->args, ctx);
    if (listLikeT(rt) || mapLikeT(rt) || setLikeT(rt))
      return emitContainerCall(rt, name, recv, e->args, ctx);
    if (rb == "Scanner") return emitScannerCall(name, recv, e->args, ctx);
    if (rt->dims > 0) {
      if (name == "length") return recv + ".size()";
      if (name == "clone") return recv + " /* clone */";
      ctx.warn(e->line, "array." + name + " unsupported; emitted as-is");
      return recv + "." + name + "(" + joinArgs(e->args, ctx) + ")";
    }
    if (excLike(rb)) {
      if (name == "getMessage") return recv + ".what()";
      if (name == "printStackTrace") {
        ctx.warn(e->line, "printStackTrace -> std::cerr << what()");
        return "void()";
      }
      if (name == "toString") return recv + ".what()";
      if (name == "getCause" || name == "initCause" || name == "addSuppressed") {
        ctx.warn(e->line, "exception." + name + " unsupported");
        return "void()";
      }
    }
    // Custom-class Object methods.
    if (name == "getClass") {
      ctx.warn(e->line, "getClass() approximated with typeid");
      return "std::string(typeid(" + recv + ").name())";
    }
    std::vector<TypePtr> argTypes;
    for (auto& ag : e->args) argTypes.push_back(inferJavaType(ag, ctx));
    std::string owner;
    const Method* m = lookupMethod(ctx.syms, rb, name, e->args.size(),
                                   argTypes, &owner);
    return recv + op + sanitizeCppName(name) + "(" +
           joinArgs(e->args, ctx, m) + ")";
  }
  return recv + op + sanitizeCppName(name) + "(" + joinArgs(e->args, ctx) + ")";
}

// ---------------------------------------------------------------------------
// New-object / new-array emission
// ---------------------------------------------------------------------------
Type elemTypeOf(const Type& t) {
  Type el = t;
  el.dims = 0;
  el.fixedSize = 0;
  el.arrSize = nullptr;
  return el;
}

std::string emitNew(const ExprPtr& e, GenCtx& ctx) {
  if (!e || !e->type) return "std::any()";
  const Type& T = *e->type;
  std::string b = lastSegName(T.base);

  if (listLikeT(e->type) || mapLikeT(e->type) || setLikeT(e->type)) {
    std::string el;
    if (!T.args.empty()) el = mapTypeImpl(T.args[0], ctx);
    if (e->k == Expr::K::NewArr && !e->args.empty() && !el.empty()) {
      std::string out = "std::vector<" + el + ">{";
      for (size_t i = 0; i < e->args.size(); ++i) {
        if (i) out += ", ";
        out += emitExpr(e->args[i], ctx);
      }
      return out + "}";
    }
    if (T.arrSize) {
      std::string el2 = el.empty() ? "auto" : el;
      return "std::vector<" + el2 + ">(" + emitExpr(T.arrSize, ctx) + ")";
    }
    return "{}";
  }

  if (b == "String") {
    if (!e->args.empty()) return "std::string(" + joinArgs(e->args, ctx) + ")";
    return "std::string()";
  }
  if (b == "StringBuilder" || b == "StringBuffer")
    return "std::string()";
  if (b == "Integer" || b == "Long" || b == "Short" || b == "Byte" ||
      b == "Float" || b == "Double" || b == "Boolean" || b == "Character") {
    if (!e->args.empty()) return emitExpr(e->args[0], ctx);
    return "0";
  }
  if (b == "Object" || (b == "Object" && T.dims > 0)) return "std::any()";

  if (excLike(b)) {
    std::string mapped = mapJ2C(T, ctx.typeParams);
    if (!e->args.empty()) return mapped + "(" + joinArgs(e->args, ctx) + ")";
    return mapped + "(\"\")";
  }

  if (T.dims > 0) {
    std::string el = mapTypeImpl(elemTypeOf(T), ctx);
    if (e->k == Expr::K::NewArr && !e->args.empty()) {
      std::string out = "std::vector<" + el + ">{";
      for (size_t i = 0; i < e->args.size(); ++i) {
        if (i) out += ", ";
        out += emitExpr(e->args[i], ctx);
      }
      return out + "}";
    }
    if (T.arrSize)
      return "std::vector<" + el + ">(" + emitExpr(T.arrSize, ctx) + ")";
    return "std::vector<" + el + ">()";
  }

  // Custom class.
  std::string nm = sanitizeCppName(b);
  if (!T.args.empty()) {
    nm += "<";
    for (size_t i = 0; i < T.args.size(); ++i) {
      if (i) nm += ", ";
      nm += mapTypeImpl(T.args[i], ctx);
    }
    nm += ">";
  }
  if (!e->args.empty()) return nm + "(" + joinArgs(e->args, ctx) + ")";
  return nm + "()";
}

// ---------------------------------------------------------------------------
// Member access emission
// ---------------------------------------------------------------------------
std::string emitMember(const ExprPtr& e, GenCtx& ctx) {
  ExprPtr obj = e->obj;
  std::string nm = sanitizeCppName(e->name);
  if (!obj) return nm;
  if (obj->k == Expr::K::This) return "this->" + nm;
  if (obj->k == Expr::K::Name) {
    if (obj->name == "super") {
      std::string base = firstBaseName(ctx);
      return (base.empty() ? "this->" : base + "::") + nm;
    }
    if (isEnumName(obj->name, ctx)) return obj->name + "::" + e->name;
    if (obj->name == "Math") {
      if (e->name == "PI") return "3.141592653589793";
      if (e->name == "E") return "2.718281828459045";
    }
    if (obj->name == "Integer") {
      if (e->name == "MAX_VALUE") return "2147483647";
      if (e->name == "MIN_VALUE") return "(-2147483647 - 1)";
    }
    if (obj->name == "Long") {
      if (e->name == "MAX_VALUE") return "9223372036854775807LL";
      if (e->name == "MIN_VALUE") return "(-9223372036854775807LL - 1)";
    }
    if (obj->name == "Double") {
      if (e->name == "MAX_VALUE") return "1.7976931348623157e308";
      if (e->name == "MIN_VALUE") return "-1.7976931348623157e308";
      if (e->name == "NaN")
        return "std::numeric_limits<double>::quiet_NaN()";
      if (e->name == "POSITIVE_INFINITY" || e->name == "NEGATIVE_INFINITY")
        return "std::numeric_limits<double>::infinity()";
    }
    if (obj->name == "Boolean") {
      if (e->name == "TRUE") return "true";
      if (e->name == "FALSE") return "false";
    }
  }
  // array length: arr.length -> arr.size()
  TypePtr ot = inferJavaType(obj, ctx);
  if (e->name == "length" && ot && ot->dims > 0)
    return baseExpr(obj, ctx) + ".size()";
  return baseExpr(obj, ctx) + acOp(obj, ctx) + nm;
}

// ---------------------------------------------------------------------------
// Expression emission (public within this TU)
// ---------------------------------------------------------------------------
std::string emitExpr(const ExprPtr& e, GenCtx& ctx) {
  if (!e) return "";
  switch (e->k) {
    case Expr::K::Name: {
      // Plain name: prefer a local/field; otherwise resolve a static
      // member reference (Java: Math.PI, Integer.MAX_VALUE, ...).
      TypePtr t = inferJavaType(e, ctx);
      if (t) return sanitizeCppName(e->name);
      // enum constant?
      if (ctx.prog) {
        for (auto& en : ctx.prog->enums)
          for (auto& v : en->values)
            if (v == e->name) return en->name + "::" + e->name;
      }
      // static field on a known class: Class::field
      if (!ctx.curClass.empty() && ctx.syms.count(e->name) == 0) {
        for (auto& kv : ctx.syms) {
          for (auto& f : kv.second.ownFields) {
            if (f.first == e->name)
              return sanitizeCppName(kv.first) + "::" + sanitizeCppName(e->name);
          }
        }
      }
      return sanitizeCppName(e->name);
    }
    case Expr::K::This: return "*this";
    case Expr::K::Null: return "nullptr";
    case Expr::K::BoolLit: return e->bval ? "true" : "false";
    case Expr::K::IntLit:
    case Expr::K::FloatLit: return e->sval;
    case Expr::K::StrLit: return strLit(e->sval);
    case Expr::K::CharLit: return "'" + litEscape(e->sval) + "'";
    case Expr::K::Paren: return "(" + emitExpr(e->lhs, ctx) + ")";
    case Expr::K::Ternary:
      return "(" + emitExpr(e->lhs, ctx) + " ? " + emitExpr(e->rhs, ctx) +
             " : " + emitExpr(e->args.empty() ? nullptr : e->args[0], ctx) + ")";
    case Expr::K::ArrayInit: {
      std::string out = "{";
      for (size_t i = 0; i < e->args.size(); ++i) {
        if (i) out += ", ";
        out += emitExpr(e->args[i], ctx);
      }
      return out + "}";
    }
    case Expr::K::Cast: {
      return "static_cast<" + mapType(e->type, ctx) + ">(" +
             emitExpr(e->lhs, ctx) + ")";
    }
    case Expr::K::Unary: {
      if (e->op == "!" || e->op == "-" || e->op == "+" || e->op == "~")
        return "(" + e->op + emitExpr(e->lhs, ctx) + ")";
      return "(" + emitExpr(e->lhs, ctx) + e->op + ")";
    }
    case Expr::K::IncDec: {
      std::string v = emitExpr(e->lhs, ctx);
      if (e->isPrefix) return "(" + e->op + v + ")";
      return "(" + v + e->op + ")";
    }
    case Expr::K::Binary: {
      if (e->op == "+") {
        TypePtr t = inferJavaType(e, ctx);
        if (isStrT(t)) return emitConcat(e, ctx);
      }
      return "(" + emitExpr(e->lhs, ctx) + " " + e->op + " " +
             emitExpr(e->rhs, ctx) + ")";
    }
    case Expr::K::Assign: {
      TypePtr lt = inferJavaType(e->lhs, ctx);
      if (e->rhs && e->rhs->k == Expr::K::NewObj && lt &&
          isInterfaceName(lastSegName(lt->base), ctx)) {
        std::string cn = sanitizeCppName(lastSegName(e->rhs->type->base));
        return "(" + emitExpr(e->lhs, ctx) + " = std::make_shared<" + cn +
               ">(" + joinArgs(e->rhs->args, ctx) + "))";
      }
      return "(" + emitExpr(e->lhs, ctx) + " " + e->op + " " +
             emitExpr(e->rhs, ctx) + ")";
    }
    case Expr::K::Index:
      return baseExpr(e->obj, ctx) + "[" + emitExpr(e->index, ctx) + "]";
    case Expr::K::Member: return emitMember(e, ctx);
    case Expr::K::Call: return emitCall(e, ctx);
    case Expr::K::NewObj:
    case Expr::K::NewArr: return emitNew(e, ctx);
    default: return "";
  }
}

// ---------------------------------------------------------------------------
// Statement emission
// ---------------------------------------------------------------------------
bool isPrintCall(const ExprPtr& e) {
  if (!e || e->k != Expr::K::Call || !e->callee ||
      e->callee->k != Expr::K::Member || !e->callee->obj)
    return false;
  const Expr& c = *e->callee;
  if (c.name != "println" && c.name != "print" && c.name != "printf")
    return false;
  const ExprPtr& obj = c.obj;
  if (obj->k != Expr::K::Member || !obj->obj) return false;
  return obj->name == "out" || obj->name == "err";
}

void emitPrint(const StmtPtr& st, GenCtx& ctx, Out& o) {
  const Expr& cal = *st->expr->callee;
  std::string stream = cal.obj->name == "err" ? "std::cerr" : "std::cout";
  bool isNewline = cal.name == "println";
  if (cal.name == "printf") {
    ctx.warn(st->line, "System.out.printf emitted via std::printf");
    std::string jo;
    for (size_t i = 0; i < st->expr->args.size(); ++i) {
      if (i) jo += ", ";
      jo += emitExpr(st->expr->args[i], ctx);
    }
    o.line("std::printf(" + jo + ");");
    return;
  }
  std::string out;
  for (size_t i = 0; i < st->expr->args.size(); ++i) {
    const ExprPtr& a = st->expr->args[i];
    TypePtr t = inferJavaType(a, ctx);
    std::string s;
    if (t && lastSegName(t->base) == "boolean")
      s = "(" + emitExpr(a, ctx) + " ? \"true\" : \"false\")";
    else
      s = emitExpr(a, ctx);
    out += " << " + s;
  }
  if (isNewline) out += " << std::endl";
  o.line(stream + out + ";");
}

void emitNested(const StmtPtr& st, GenCtx& ctx, Out& o) {
  if (!st) { o.line("{}"); return; }
  if (st->k == Stmt::K::Block) { emitStmt(st, ctx, o); return; }
  ++o.indent;
  emitStmt(st, ctx, o);
  --o.indent;
}

void emitVarDecl(const StmtPtr& st, GenCtx& ctx, Out& o) {
  const VarDeclS& vd = st->vdecl;
  TypePtr ty = vd.type;
  std::string tname = mapType(ty, ctx);
  bool ifaceTarget = ty && isInterfaceName(lastSegName(ty->base), ctx);
  for (size_t d = 0; d < vd.decls.size(); ++d) {
    const Declarator& dc = vd.decls[d];
    std::string frag;
    if (dc.init) {
      const ExprPtr& in = dc.init;
      if (ifaceTarget) {
        if (in->k == Expr::K::Null) {
          frag = " = nullptr";
        } else if (in->k == Expr::K::NewObj) {
          std::string cn = sanitizeCppName(lastSegName(in->type->base));
          frag = " = std::make_shared<" + cn + ">(" +
                 joinArgs(in->args, ctx) + ")";
        } else {
          ctx.warn(st->line, "interface field init not a constructor; skipping");
          frag = "";
        }
      } else if (in->k == Expr::K::NewObj || in->k == Expr::K::NewArr) {
        if (listLikeT(ty) || mapLikeT(ty) || setLikeT(ty) ||
            (ty && ty->dims > 0)) {
          const Type& T = *in->type;
          if (in->k == Expr::K::NewArr && !in->args.empty()) {
            std::string el = ty && ty->dims > 0
              ? mapTypeImpl(elemTypeOf(*ty), ctx)
              : (T.args.empty() ? "auto" : mapTypeImpl(T.args[0], ctx));
            std::string out = "{";
            for (size_t i = 0; i < in->args.size(); ++i) {
              if (i) out += ", ";
              out += emitExpr(in->args[i], ctx);
            }
            frag = " " + out + "}";
          } else if (T.arrSize) {
            frag = "(" + emitExpr(T.arrSize, ctx) + ")";
          } else {
            frag = "{}";
          }
        } else {
          frag = " = " + emitNew(in, ctx);
        }
      } else if (in->k == Expr::K::ArrayInit) {
        std::string out = "{";
        for (size_t i = 0; i < in->args.size(); ++i) {
          if (i) out += ", ";
          out += emitExpr(in->args[i], ctx);
        }
        frag = " " + out + "}";
      } else {
        frag = " = " + emitExpr(in, ctx);
      }
    }
    o.line(tname + " " + sanitizeCppName(dc.name) + frag + ";");
    ctx.locals.add(dc.name, ty);
  }
}

void emitSwitch(const StmtPtr& st, GenCtx& ctx, Out& o) {
  TypePtr cty = inferJavaType(st->swCond, ctx);
  std::string cond = emitExpr(st->swCond, ctx);
  if (isStrT(cty)) {
    bool first = true;
    for (auto& cs : st->swCases) {
      if (cs.isDefault) continue;
      std::string h = first ? "if (" : "else if (";
      first = false;
      std::string v = (cs.value && cs.value->k == Expr::K::StrLit)
        ? strLit(cs.value->sval)
        : (cs.value ? emitExpr(cs.value, ctx) : std::string("\"\""));
      o.line(h + cond + " == " + v + ") {");
      ++o.indent;
      for (auto& s : cs.stmts) {
        if (s && (s->k == Stmt::K::Break || s->k == Stmt::K::Empty)) continue;
        emitStmt(s, ctx, o);
      }
      --o.indent;
      o.line("}");
    }
    for (auto& cs : st->swCases) {
      if (!cs.isDefault) continue;
      o.line(first ? "{" : "else {");
      ++o.indent;
      for (auto& s : cs.stmts) {
        if (s && (s->k == Stmt::K::Break || s->k == Stmt::K::Empty)) continue;
        emitStmt(s, ctx, o);
      }
      --o.indent;
      o.line("}");
      break;
    }
    return;
  }
  o.line("switch (" + cond + ") {");
  for (auto& cs : st->swCases) {
    if (cs.isDefault) o.line("default:");
    else o.line("case " + (cs.value ? emitExpr(cs.value, ctx)
                                    : std::string("0")) + ":");
    ++o.indent;
    for (auto& s : cs.stmts) emitStmt(s, ctx, o);
    --o.indent;
  }
  o.line("}");
}

void emitForEach(const StmtPtr& st, GenCtx& ctx, Out& o) {
  const ExprPtr& coll = st->eachColl;
std::string nm = sanitizeCppName(st->eachName);
      bool adapter = false;
      if (coll && coll->k == Expr::K::Call && coll->callee &&
          coll->callee->k == Expr::K::Member) {
        const Expr& c = *coll->callee;
        TypePtr rt = inferJavaType(c.obj, ctx);
        if (mapLikeT(rt)) {
          std::string m = c.name;
          std::string recv = baseExpr(c.obj, ctx);
          if (m == "keySet" || m == "values" || m == "entrySet") {
            adapter = true;
            o.line("for (const auto& kv : " + recv + ") {");
            ++o.indent;
            if (m == "entrySet") {
              ctx.warn(st->line,
                       "entrySet iteration approximated: use kv.first / kv.second");
              o.line("const auto& " + nm + " = kv;");
            } else if (m == "keySet") {
              o.line("const auto& " + nm + " = kv.first;");
            } else {
              o.line("const auto& " + nm + " = kv.second;");
            }
            if (st->body && st->body->k == Stmt::K::Block) {
              if (st->body->block)
                for (auto& s : st->body->block->stmts) emitStmt(s, ctx, o);
            } else {
              emitStmt(st->body, ctx, o);
            }
            --o.indent;
            o.line("}");
            ctx.locals.add(st->eachName, st->eachType);
            return;
          }
        }
      }
      o.line("for (const auto& " + nm + " : " + emitExpr(coll, ctx) + ") {");
      ++o.indent;
      if (st->body && st->body->k == Stmt::K::Block) {
        if (st->body->block)
          for (auto& s : st->body->block->stmts) emitStmt(s, ctx, o);
      } else {
        emitStmt(st->body, ctx, o);
      }
      --o.indent;
      o.line("}");
      ctx.locals.add(st->eachName, st->eachType);
    }

void emitTry(const StmtPtr& st, GenCtx& ctx, Out& o) {
  o.line("try {");
  ++o.indent;
  if (st->tryBlock) for (auto& s : st->tryBlock->stmts) emitStmt(s, ctx, o);
  --o.indent;
  o.line("}");
  for (auto& ct : st->catches) {
    o.line("catch (const " + mapType(ct.type, ctx) + "& " +
           sanitizeCppName(ct.name) + ") {");
    ++o.indent;
    if (ct.body) {
      ctx.locals.push();
      ctx.locals.add(ct.name, ct.type);
      for (auto& s : ct.body->stmts) emitStmt(s, ctx, o);
      ctx.locals.pop();
    }
    --o.indent;
    o.line("}");
  }
  if (st->finallyBlock) {
    ctx.warn(st->line, "finally is not executable in C++; statements skipped "
             "(see README limitations)");
    o.line("/* finally skipped */");
  }
}

void emitStmt(const StmtPtr& st, GenCtx& ctx, Out& o) {
  if (!st) return;
  switch (st->k) {
    case Stmt::K::Block: {
      ctx.locals.push();
      o.line("{");
      ++o.indent;
      if (st->block) for (auto& s : st->block->stmts) emitStmt(s, ctx, o);
      --o.indent;
      o.line("}");
      ctx.locals.pop();
      return;
    }
    case Stmt::K::VarDecl:
      emitVarDecl(st, ctx, o);
      return;
    case Stmt::K::Expr: {
      if (isPrintCall(st->expr)) { emitPrint(st, ctx, o); return; }
      if (st->expr && st->expr->k == Expr::K::Call && st->expr->callee &&
          st->expr->callee->k == Expr::K::Member) {
        const Expr& c = *st->expr->callee;
        TypePtr rt = inferJavaType(c.obj, ctx);
        if (c.name == "printStackTrace" && rt &&
            excLike(lastSegName(rt->base))) {
          o.line("std::cerr << " + emitExpr(c.obj, ctx) + ".what() << std::endl;");
          return;
        }
        if (c.name == "exit" && c.obj && c.obj->k == Expr::K::Name &&
            c.obj->name == "System") {
          o.line("std::exit(" +
                 (st->expr->args.empty() ? "0"
                                         : emitExpr(st->expr->args[0], ctx)) +
                 ");");
          return;
        }
        if (c.name == "close" && rt &&
            lastSegName(rt->base) == "Scanner") {
          o.line("/* scanner closed */");
          return;
        }
      }
      std::string s = emitExpr(st->expr, ctx);
      if (s.empty() || s == "void()") { o.line(";"); return; }
      o.line(s + ";");
      return;
    }
    case Stmt::K::If: {
      o.line("if (" + emitExpr(st->cond, ctx) + ")");
      emitNested(st->thenS, ctx, o);
      if (st->elseS) {
        o.line("else");
        emitNested(st->elseS, ctx, o);
      }
      return;
    }
    case Stmt::K::While: {
      o.line("while (" + emitExpr(st->cond, ctx) + ")");
      emitNested(st->body, ctx, o);
      return;
    }
    case Stmt::K::Do: {
      o.line("do");
      emitNested(st->body, ctx, o);
      o.line("while (" + emitExpr(st->cond, ctx) + ");");
      return;
    }
    case Stmt::K::For: {
      std::string init;
      if (st->hasForInitV) {
        const VarDeclS& vd = st->forInitV;
        init = mapType(vd.type, ctx) + " ";
        for (size_t i = 0; i < vd.decls.size(); ++i) {
          if (i) init += ", ";
          init += sanitizeCppName(vd.decls[i].name);
          if (vd.decls[i].init)
            init += " = " + emitExpr(vd.decls[i].init, ctx);
          if (!vd.decls[i].init || true) {
            // register the loop variable in the symbol table
          }
        }
        for (size_t i = 0; i < vd.decls.size(); ++i)
          ctx.locals.add(vd.decls[i].name, vd.type);
      } else if (st->forInitE) {
        init = emitExpr(st->forInitE, ctx);
      }
      std::string cond = st->forCond ? emitExpr(st->forCond, ctx) : "";
      std::string inc = st->forInc ? emitExpr(st->forInc, ctx) : "";
      o.line("for (" + init + "; " + cond + "; " + inc + ")");
      ctx.locals.push();
      emitNested(st->body, ctx, o);
      ctx.locals.pop();
      return;
    }
    case Stmt::K::ForEach:
      emitForEach(st, ctx, o);
      return;
    case Stmt::K::Switch:
      emitSwitch(st, ctx, o);
      return;
    case Stmt::K::Break: o.line("break;"); return;
    case Stmt::K::Continue: o.line("continue;"); return;
    case Stmt::K::Return: {
      if (st->hasValue) {
        const ExprPtr& rv = st->expr;
        if (rv && rv->k == Expr::K::NewObj && ctx.retType &&
            isInterfaceName(lastSegName(ctx.retType->base), ctx)) {
          std::string cn = sanitizeCppName(lastSegName(rv->type->base));
          o.line("return std::make_shared<" + cn + ">(" +
                 joinArgs(rv->args, ctx) + ");");
          return;
        }
        o.line("return " + emitExpr(rv, ctx) + ";");
      } else {
        o.line("return;");
      }
      return;
    }
    case Stmt::K::Throw:
      o.line("throw " + emitExpr(st->expr, ctx) + ";");
      return;
    case Stmt::K::Try:
      emitTry(st, ctx, o);
      return;
    case Stmt::K::Empty: o.line(";"); return;
    case Stmt::K::Label:
      ctx.warn(st->line, "label skipped: " + st->label);
      return;
    default:
      return;
  }
}

// ---------------------------------------------------------------------------
// Constant-expression check (for static final fields).
// ---------------------------------------------------------------------------
bool isConstExpr(const ExprPtr& e, GenCtx& ctx) {
  if (!e) return false;
  switch (e->k) {
    case Expr::K::IntLit: case Expr::K::FloatLit: case Expr::K::BoolLit:
    case Expr::K::StrLit: case Expr::K::CharLit:
      return true;
    case Expr::K::Paren: return isConstExpr(e->lhs, ctx);
    case Expr::K::Unary:
      return e->op == "-" || e->op == "+" || e->op == "!" || e->op == "~";
    case Expr::K::Binary:
      return e->op == "+" || e->op == "-" || e->op == "*" || e->op == "/" ||
             e->op == "%" || e->op == "<<" || e->op == ">>" ||
             e->op == "&" || e->op == "|" || e->op == "^";
    case Expr::K::Name: return true;   // references another constant
    case Expr::K::Member: return true; // Integer.MAX_VALUE etc.
    case Expr::K::Ternary: return isConstExpr(e->rhs, ctx) &&
                                   isConstExpr(e->args.empty() ? nullptr
                                                               : e->args[0], ctx);
    default: return false;
  }
}

// ---------------------------------------------------------------------------
// Member / field emission
// ---------------------------------------------------------------------------
void emitField(const Field& f, GenCtx& ctx, Out& o) {
  bool isStatic = hasMod(f.mods, "static");
  bool isFinal = hasMod(f.mods, "final");
  std::string nm = sanitizeCppName(f.name);
  std::string ty = mapTypeImpl(*f.type, ctx);
  std::string head;
  bool stringField = f.type && f.type->dims == 0 && isStrT(f.type);
  if (isStatic && isFinal && stringField) {
    head = "static inline const std::string";
  } else if (isStatic && isFinal && isConstExpr(f.init, ctx)) {
    head = "static constexpr " + ty;
  } else if (isStatic && isFinal) {
    head = "static inline " + ty;
  } else if (isStatic) {
    head = "static inline " + ty;
  } else {
    head = ty;
  }
  std::string frag;
  if (f.init) {
    const ExprPtr& in = f.init;
    if (isStatic && isFinal && stringField &&
        in->k == Expr::K::StrLit) {
      frag = "(\"" + litEscape(in->sval) + "\")";
    } else if (in->k == Expr::K::NewObj || in->k == Expr::K::NewArr) {
      if (listLikeT(f.type) || mapLikeT(f.type) || setLikeT(f.type) ||
          (f.type && f.type->dims > 0)) {
        const Type& T = *in->type;
        if (in->k == Expr::K::NewArr && !in->args.empty()) {
          std::string el = f.type && f.type->dims > 0
            ? mapTypeImpl(elemTypeOf(*f.type), ctx)
            : (T.args.empty() ? "auto" : mapTypeImpl(T.args[0], ctx));
          std::string out = "{";
          for (size_t i = 0; i < in->args.size(); ++i) {
            if (i) out += ", ";
            out += emitExpr(in->args[i], ctx);
          }
          frag = out + "}";
        } else if (T.arrSize) {
          frag = "(" + emitExpr(T.arrSize, ctx) + ")";
        } else {
          frag = "{}";
        }
      } else {
        frag = " = " + emitNew(in, ctx);
      }
    } else if (in->k == Expr::K::ArrayInit) {
      std::string out = "{";
      for (size_t i = 0; i < in->args.size(); ++i) {
        if (i) out += ", ";
        out += emitExpr(in->args[i], ctx);
      }
      frag = out + "}";
    } else {
      frag = " = " + emitExpr(in, ctx);
    }
  }
  o.line(head + " " + nm + (frag.empty() ? "" : (" " + frag)) + ";");
}

// ---------------------------------------------------------------------------
// Method emission
// ---------------------------------------------------------------------------
void emitMethod(const ClassDecl& c, const Method& m, GenCtx& ctx, Out& o,
                bool inInterface) {
  std::string pre;
  bool isStatic = hasMod(m.mods, "static");
  bool isPrivate = hasMod(m.mods, "private");
  bool isFinal = hasMod(m.mods, "final");
  bool isAbstract = hasMod(m.mods, "abstract") || inInterface;
  bool polymorphic = inInterface ||
                     (!isStatic && !isPrivate && !isFinal &&
                      !hasMod(c.mods, "final"));
  if (isStatic) pre += "static ";
  else if (polymorphic) pre += "virtual ";

  std::string tparams;
  if (!m.typeParams.empty()) {
    tparams = "template <";
    for (size_t i = 0; i < m.typeParams.size(); ++i) {
      if (i) tparams += ", ";
      tparams += "typename " + sanitizeCppName(m.typeParams[i]);
    }
    tparams += ">";
  }

  std::string paramsOut;
  for (size_t i = 0; i < m.params.size(); ++i) {
    if (i) paramsOut += ", ";
    const Param& p = m.params[i];
    if (p.varargs) {
      paramsOut += "std::vector<" + mapTypeImpl(*p.type, ctx) + "> " +
                   sanitizeCppName(p.name);
    } else {
      paramsOut += mapType(p.type, ctx) + " " + sanitizeCppName(p.name);
    }
  }

  std::string mname = m.isCtor ? sanitizeCppName(c.name)
                               : sanitizeCppName(m.name);
  std::string ret = m.isCtor ? sanitizeCppName(c.name)
                             : (m.ret ? mapType(m.ret, ctx) : "void");

  // Special: Java main(String[] args) -> C++ main.
  bool cppMain = false;
  if (!m.isCtor && m.name == "main" && isStatic &&
      (!m.ret || lastSegName(m.ret->base) == "void") &&
      m.params.size() == 1 && m.params[0].type &&
      m.params[0].type->dims > 0 &&
      lastSegName(m.params[0].type->base) == "String") {
    cppMain = true;
    ret = "int";
    mname = "main";
    paramsOut = "int argc, char** argv";
    pre = "";
  }

  std::string throwsCmt;
  for (auto& t : m.throws) {
    if (!throwsCmt.empty()) throwsCmt += ", ";
    throwsCmt += lastSegName(t->base);
  }

  std::string sig = (m.isCtor ? mname : ret + " " + mname) + "(" + paramsOut + ")";

  if (!tparams.empty()) o.line(tparams);
  if (!throwsCmt.empty() && !m.body)
    o.line("// throws " + throwsCmt);

  // Abstract / interface method without body.
  if (!m.body || (isAbstract && !m.isCtor && !m.body)) {
    std::string decl = sig;
    if (isAbstract && !m.isCtor) decl += " = 0";
    o.line(pre + decl + ";");
    return;
  }

  // Method with body.
  TypePtr savedRet = ctx.retType;
  ctx.retType = m.ret;
  ctx.locals.push();
  for (auto& p : m.params) ctx.locals.add(p.name, p.type);

  // Constructor: extract super(...) into init list.
  std::string init;
  size_t skipFirst = 0;
  if (m.isCtor && m.body && !m.body->stmts.empty()) {
    const StmtPtr& s0 = m.body->stmts[0];
    if (s0 && s0->k == Stmt::K::Expr && s0->expr &&
        s0->expr->k == Expr::K::Call && s0->expr->callee &&
        s0->expr->callee->k == Expr::K::Name &&
        s0->expr->callee->name == "super") {
      std::string base = firstBaseName(ctx);
      if (base.empty()) base = sanitizeCppName(c.name);
      init = " : " + base + "(" + joinArgs(s0->expr->args, ctx) + ")";
      skipFirst = 1;
    }
  }

  if (cppMain) {
    // Java's static main(String[]) maps to a free C++ main(), emitted at
    // global scope (outside the class) through ctx.mainOut.
    Out& mo = ctx.mainOut ? *ctx.mainOut : o;
    mo.open("int main(int argc, char** argv)");
    std::string argsName = sanitizeCppName(m.params[0].name);
    mo.line("std::vector<std::string> " + argsName + "(argv + 1, argv + argc);");
    for (size_t i = skipFirst; i < m.body->stmts.size(); ++i)
      emitStmt(m.body->stmts[i], ctx, mo);
    mo.closePlain();
  } else {
    o.open((m.isCtor ? mname : pre + ret + " " + mname) + "(" + paramsOut + ")" + init);
    for (size_t i = skipFirst; i < m.body->stmts.size(); ++i)
      emitStmt(m.body->stmts[i], ctx, o);
    o.closePlain();
  }
  ctx.locals.pop();
  ctx.retType = savedRet;
}

// ---------------------------------------------------------------------------
// Class emission
// ---------------------------------------------------------------------------
std::string inheritList(const ClassDecl& c, GenCtx& ctx) {
  std::string out;
  for (auto& b : c.bases) {
    if (!b.type) continue;
    std::string bn = lastSegName(b.type->base);
    if (excLike(bn)) {
      out += "public std::runtime_error";
      continue;
    }
    if (isJavaBuiltin(bn)) continue;   // List, Comparable ... -> std:: / skip
    std::string nm = bn;
    if (!b.type->args.empty()) {
      nm += "<";
      for (size_t i = 0; i < b.type->args.size(); ++i) {
        if (i) nm += ", ";
        nm += mapTypeImpl(b.type->args[i], ctx);
      }
      nm += ">";
    }
    if (!out.empty()) out += ", ";
    out += "public " + nm;
  }
  return out.empty() ? "" : " : " + out;
}

void emitClass(const std::shared_ptr<ClassDecl>& c, GenCtx& ctx, Out& o,
               std::set<std::string>& emitted) {
  if (!c) return;
  std::string cn = sanitizeCppName(c->name);
  if (emitted.count(cn)) return;
  emitted.insert(cn);

  // Emit base classes first (dependency order for inheritance).
  if (ctx.prog) {
    for (auto& b : c->bases) {
      if (!b.type) continue;
      std::string bn = lastSegName(b.type->base);
      for (auto& pc : ctx.prog->classes) {
        if (pc->name == bn) { emitClass(pc, ctx, o, emitted); break; }
      }
    }
  }

  std::vector<std::string> savedTP = ctx.typeParams;
  ctx.typeParams = c->typeParams;
  ctx.curClass = c->name;
  for (auto& p : c->typeParams) ctx.locals.add(p, nullptr);

  std::string inherits = inheritList(*c, ctx);
  if (!c->typeParams.empty()) {
    std::string tp = "template <";
    for (size_t i = 0; i < c->typeParams.size(); ++i) {
      if (i) tp += ", ";
      tp += "typename " + sanitizeCppName(c->typeParams[i]);
    }
    tp += ">";
    o.line(tp);
  }
  o.open("class " + cn + inherits);

  struct Item {
    char kind;
    const Field* f = nullptr;
    const Method* m = nullptr;
  };
  std::vector<Item> pub, prot, priv;
  auto accOf = [&](const std::vector<std::string>& mods) {
    // Java interface members are implicitly public abstract / public static final.
    if (c->isInterface) return "public";
    if (hasMod(mods, "protected")) return "protected";
    if (hasMod(mods, "private")) return "private";
    return "public";
  };
  for (auto& f : c->fields) {
    Item it; it.kind = 'f'; it.f = &f;
    std::string a = accOf(f.mods);
    if (a == "protected") prot.push_back(it);
    else if (a == "private") priv.push_back(it);
    else pub.push_back(it);
  }
  for (auto& m : c->methods) {
    Item it; it.kind = 'm'; it.m = &m;
    std::string a = accOf(m.mods);
    if (a == "protected") prot.push_back(it);
    else if (a == "private") priv.push_back(it);
    else pub.push_back(it);
  }
  auto emitItems = [&](std::vector<Item>& items, const char* hdr) {
    if (items.empty()) return;
    o.line(hdr);
    for (auto& it : items) {
      if (it.kind == 'f') emitField(*it.f, ctx, o);
      else emitMethod(*c, *it.m, ctx, o, c->isInterface);
    }
  };
  emitItems(pub, "public:");
  emitItems(prot, "protected:");
  emitItems(priv, "private:");

  // Interfaces still need a virtual destructor.
  if (c->isInterface)
    o.line("virtual ~" + cn + "() {}");

  // 'using Base::name;' so base overloads remain visible when the subclass
  // redefines a same-named method (Java overload resolution across levels).
  std::set<std::string> ourNames;
  for (auto& m : c->methods) ourNames.insert(m.name);
  const ClassInfo* ci = findClass(ctx.syms, c->name);
  if (ci && !ourNames.empty() && ctx.prog) {
    for (auto& b : c->bases) {
      if (!b.type) continue;
      std::string bn = lastSegName(b.type->base);
      const ClassInfo* bc = findClass(ctx.syms, bn);
      if (!bc || bc->isInterface) continue;
      for (auto& ms : bc->ownMethods) {
        if (ourNames.count(ms.name))
          o.line("using " + sanitizeCppName(bn) + "::" +
                 sanitizeCppName(ms.name) + ";");
      }
    }
  }

  o.closeSemi();
  ctx.typeParams = savedTP;
  ctx.curClass = "";
}

void emitEnum(const std::shared_ptr<EnumDecl>& en, GenCtx& ctx, Out& o) {
  std::string out = "enum class " + sanitizeCppName(en->name) + " { ";
  for (size_t i = 0; i < en->values.size(); ++i) {
    if (i) out += ", ";
    out += en->values[i];
  }
  out += " };";
  o.line(out);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
std::string generateJ2C(const std::shared_ptr<Program>& p,
                        std::vector<Diag>& diags) {
  GenCtx ctx(diags);
  ctx.prog = p.get();
  ctx.syms = buildSymbols(*p, diags);

  Out o;
  o.line("// Generated by LangBridge (Java -> C++17).");
  o.line("#include <iostream>");
  o.line("#include <string>");
  o.line("#include <vector>");
  o.line("#include <map>");
  o.line("#include <set>");
  o.line("#include <optional>");
  o.line("#include <any>");
  o.line("#include <stdexcept>");
  o.line("#include <cstdlib>");
  o.line("#include <cmath>");
  o.line("#include <algorithm>");
  o.line("#include <memory>");
  o.line("#include <cstdint>");
  o.line("#include <cctype>");
  o.line("#include <cstdio>");
  o.line("#include <typeinfo>");
  o.line("#include <chrono>");
  o.line("#include <limits>");
  o.line("");
  o.line("namespace stdbridge {");
  o.line("inline bool startsWith(const std::string& s, const std::string& p) {");
  o.line("  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;");
  o.line("}");
  o.line("inline bool endsWith(const std::string& s, const std::string& p) {");
  o.line("  return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;");
  o.line("}");
  o.line("inline bool equalsIgnoreCase(const std::string& a, const std::string& b) {");
  o.line("  if (a.size() != b.size()) return false;");
  o.line("  for (size_t i = 0; i < a.size(); ++i)");
  o.line("    if (std::tolower(static_cast<unsigned char>(a[i])) !=");
  o.line("        std::tolower(static_cast<unsigned char>(b[i]))) return false;");
  o.line("  return true;");
  o.line("}");
  o.line("inline std::string toLowerCase(std::string s) {");
  o.line("  for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));");
  o.line("  return s;");
  o.line("}");
  o.line("inline std::string toUpperCase(std::string s) {");
  o.line("  for (auto& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));");
  o.line("  return s;");
  o.line("}");
  o.line("inline std::string trim(const std::string& s) {");
  o.line("  size_t a = 0, b = s.size();");
  o.line("  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;");
  o.line("  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;");
  o.line("  return s.substr(a, b - a);");
  o.line("}");
  o.line("inline std::string replaceAllS(std::string s, const std::string& from, const std::string& to) {");
  o.line("  if (from.empty()) return s;");
  o.line("  size_t pos = 0;");
  o.line("  while ((pos = s.find(from, pos)) != std::string::npos) {");
  o.line("    s.replace(pos, from.size(), to); pos += to.size();");
  o.line("  }");
  o.line("  return s;");
  o.line("}");
  o.line("inline std::string repeat(const std::string& s, int n) {");
  o.line("  std::string r; r.reserve(s.size() * static_cast<size_t>(n > 0 ? n : 0));");
  o.line("  for (int i = 0; i < n; ++i) r += s;");
  o.line("  return r;");
  o.line("}");
  o.line("inline std::vector<std::string> split(const std::string& s, const std::string& sep) {");
  o.line("  std::vector<std::string> out;");
  o.line("  if (sep.empty()) { if (!s.empty()) out.push_back(s); return out; }");
  o.line("  size_t pos = 0, f;");
  o.line("  while ((f = s.find(sep, pos)) != std::string::npos) {");
  o.line("    out.push_back(s.substr(pos, f - pos)); pos = f + sep.size();");
  o.line("  }");
  o.line("  out.push_back(s.substr(pos));");
  o.line("  return out;");
  o.line("}");
  o.line("template <typename T> inline bool containsVec(const std::vector<T>& v, const T& x) {");
  o.line("  return std::find(v.begin(), v.end(), x) != v.end();");
  o.line("}");
  o.line("template <typename T> inline long indexOfVec(const std::vector<T>& v, const T& x) {");
  o.line("  auto it = std::find(v.begin(), v.end(), x);");
  o.line("  return it == v.end() ? -1L : static_cast<long>(it - v.begin());");
  o.line("}");
  o.line("template <typename K, typename V> inline bool containsKey(const std::map<K, V>& m, const K& k) {");
  o.line("  return m.count(k) != 0;");
  o.line("}");
  o.line("} // namespace stdbridge");
  o.line("");

  std::set<std::string> emitted;
  for (auto& en : p->enums) emitEnum(en, ctx, o);
  Out m;
  ctx.mainOut = &m;
  for (auto& c : p->classes) emitClass(c, ctx, o, emitted);
  o.raw(m.str());

  return o.str();
}

} // namespace bridge