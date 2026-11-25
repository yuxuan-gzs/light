// gen_common.cpp - shared code-generation helpers (T4/T5).
#include "bridge.hpp"
#include <algorithm>

namespace bridge {

namespace {

bool containsStr(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

std::string lastSegName(const std::string& s) {
  size_t p = s.find_last_of("::");
  if (p == std::string::npos) p = s.find_last_of('.');
  return (p == std::string::npos) ? s : s.substr(p + 1);
}

// Infer the type of a call expression using the symbol table.
TypePtr inferCallType(const ExprPtr& e, const GenCtx& ctx) {
  if (!e || e->k != Expr::K::Call || !e->callee) return nullptr;
  const Expr& c = *e->callee;
  std::string owner = ctx.curClass;
  std::string mname;
  if (c.k == Expr::K::Name) {
    mname = c.name;
  } else if (c.k == Expr::K::Member && c.obj) {
    mname = c.name;
    if (c.obj->k == Expr::K::Name) {
      // obj.foo(): if obj is a local/field base, use its type as owner.
      TypePtr ot = ctx.locals.lookup(c.obj->name);
      if (!ot && !owner.empty()) {
        // check fields as fallback
        const ClassInfo* ci = findClass(ctx.syms, owner);
        if (ci) {
          auto it = ci->allFields.find(c.obj->name);
          if (it != ci->allFields.end()) ot = it->second;
        }
      }
      if (ot && !isJavaBuiltin(ot->base)) owner = lastSegName(ot->base);
    }
  } else {
    return nullptr;
  }
  // Infer argument types (best effort).
  std::vector<TypePtr> argTypes;
  for (auto& a : e->args) argTypes.push_back(inferJavaType(a, ctx));
  std::string outOwner;
  const Method* m = lookupMethod(ctx.syms, owner, mname, e->args.size(), argTypes, &outOwner);
  if (m && m->ret) return m->ret;
  return nullptr;
}

} // anonymous namespace

void GenCtx::warn(int line, const std::string& msg) {
  Diag d;
  d.line = line;
  d.msg = msg;
  d.warning = true;
  diags.push_back(d);
}

std::string litEscape(const std::string& inner) {
  // Java and C++ share the common escapes for the strings we accept.
  // The lexer keeps the inner text verbatim, so just place it back.
  return inner;
}

TypePtr inferJavaType(const ExprPtr& e, const GenCtx& ctx) {
  if (!e) return nullptr;
  auto mk = [](const std::string& b) {
    auto t = std::make_shared<Type>();
    t->base = b;
    return t;
  };
  switch (e->k) {
    case Expr::K::StrLit: return mk("String");
    case Expr::K::IntLit: return mk("int");
    case Expr::K::FloatLit: return mk("double");
    case Expr::K::BoolLit: return mk("boolean");
    case Expr::K::CharLit: return mk("char");
    case Expr::K::Null: return mk("Object");
    case Expr::K::This: return ctx.curClass.empty() ? nullptr : mk(ctx.curClass);
    case Expr::K::Name: {
      TypePtr t = ctx.locals.lookup(e->name);
      if (t) return t;
      if (!ctx.curClass.empty()) {
        TypePtr f = lookupField(ctx.syms, ctx.curClass, e->name);
        if (f) return f;
      }
      return nullptr;
    }
    case Expr::K::Call: return inferCallType(e, ctx);
    case Expr::K::Member: {
      // e.g. list.size() handled through Call; bare member not typed.
      return nullptr;
    }
    case Expr::K::Index: {
      TypePtr arr = inferJavaType(e->obj, ctx);
      // generic element: args[0] of a collection type
      if (arr && !arr->args.empty()) {
        auto el = std::make_shared<Type>(arr->args[0]);
        return el;
      }
      return nullptr;
    }
    case Expr::K::NewObj:
    case Expr::K::NewArr:
      return e->type;
    case Expr::K::Cast:
      return e->type;
    case Expr::K::Binary: {
      if (e->op == "+") {
        TypePtr l = inferJavaType(e->lhs, ctx);
        TypePtr r = inferJavaType(e->rhs, ctx);
        if (l && lastSegName(l->base) == "String") return mk("String");
        if (r && lastSegName(r->base) == "String") return mk("String");
      }
      return nullptr;
    }
    case Expr::K::Ternary:
      return inferJavaType(e->rhs, ctx);  // keep it simple
    case Expr::K::Paren:
      return inferJavaType(e->lhs, ctx);
    default:
      return nullptr;
  }
}

} // namespace bridge