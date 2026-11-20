// bridge.hpp - LangBridge core data model for Java <-> C++ conversion.
// (All code comments are ASCII-only by project convention.)
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <cstdint>

namespace bridge {

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
struct Diag {
  int line = 0;
  std::string msg;
  bool warning = false;
};

class ParseError : public std::runtime_error {
public:
  int line;
  ParseError(int ln, const std::string& msg) : std::runtime_error(msg), line(ln) {}
};

// ---------------------------------------------------------------------------
// Tokens
// ---------------------------------------------------------------------------
enum class Tok {
  Word,   // identifier or keyword (meaning decided by parser)
  Int,    // integer literal (text kept verbatim)
  Float,  // floating literal
  Str,    // string literal (payload keeps inner text verbatim, quotes not included)
  Char,   // char literal (payload keeps inner text verbatim)
  Bool,   // true / false
  Null,   // null / nullptr
  Op,     // operator or punctuation
  Eol,    // end of line (only meaningful for '#directives' handling)
  End
};

struct Token {
  Tok kind = Tok::End;
  std::string text;
  int line = 1;
};

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

struct Type;
using TypePtr = std::shared_ptr<Type>;

struct Type {
  std::string base;            // e.g. "int", "String", "std::vector", "Foo", "java.util.List"
  std::vector<Type> args;      // generic / template arguments
  int dims = 0;                // array dimensions (Java suffixes or C++ fixed arrays)
  int fixedSize = 0;           // C++ fixed array size for the first dimension (0 = dynamic)
  bool isConst = false;        // C++ const
  bool isRef = false;          // C++ reference
  bool isPtr = false;          // C++ pointer
  bool isUnsigned = false;     // C++ unsigned
  bool isVolatile = false;
  ExprPtr arrSize = nullptr;   // C++ fixed array size expression (first dim)

  std::string str() const;     // human readable spelling for diagnostics
  bool operator==(const Type& o) const;
  bool operator!=(const Type& o) const { return !(*this == o); }
};

} // namespace bridge

// (Expr forward declaration; Param below uses ExprPtr and must come after it.)
namespace bridge {

struct Expr;
using ExprPtr = std::shared_ptr<Expr>;

// Lambda bodies are compound statements; forward-declared here so Expr can
// hold one without pulling in the full Stmt/Block definitions.
struct Block;
using BlockPtr = std::shared_ptr<Block>;

// Lambda parameter (name + optional type); 'varargs' is unused for C++ but
// kept for symmetry with Param.
struct LambdaP {
  TypePtr type;
  std::string name;
  bool varargs = false;
};

struct Expr {
  enum class K {
    Name, This, Null, BoolLit, IntLit, FloatLit, StrLit, CharLit,
    Binary, Unary, Assign, IncDec, Call, Member, Scope, Arrow, Index,
    NewObj, NewArr, Cast, Ternary, ArrayInit, Paren, Lambda
  };
  K k = K::Name;
  std::string name;       // Name text / member name / method name
  std::string op;         // operator text for Binary/Unary/Assign/IncDec
  int64_t ival = 0;
  double fval = 0.0;
  bool bval = false;
  std::string sval;       // string/char literal payload (verbatim inner text)
  ExprPtr obj;            // base of Member/Scope/Arrow/Index
  ExprPtr lhs, rhs;       // Binary(lhs,rhs); Assign(lhs,rhs); Unary(lhs);
                          // Cast(lhs=operand); Ternary(lhs=cond, rhs=then, args[0]=else)
  ExprPtr callee;         // Call target expression
  ExprPtr index;          // Index expression for Index nodes
  std::vector<ExprPtr> args;
  TypePtr type;           // NewObj/NewArr/Cast
  ExprPtr sizeEx = nullptr; // NewArr: size expression (when created with a size)
  bool isPrefix = true;   // IncDec prefix/postfix
  // Lambda: parameters, body, trailing return type text, raw capture list.
  std::vector<LambdaP> lparams;
  BlockPtr lbody = nullptr;
  std::string lret;
  std::string lcapture;
  int line = 0;
};

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
struct Param {
  TypePtr type;
  std::string name;
  ExprPtr def = nullptr;  // default argument (C++)
  bool varargs = false;   // "..." after type (Java) or "..." (C++)
};

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------
struct Stmt;
using StmtPtr = std::shared_ptr<Stmt>;
struct Block { std::vector<StmtPtr> stmts; };

struct Declarator {
  std::string name;
  ExprPtr init;           // may be null
};

struct VarDeclS {
  TypePtr type;
  std::vector<Declarator> decls;
};

struct CatchClause {
  TypePtr type;
  std::string name;
  BlockPtr body;
  bool isCatchAll = false;  // C++ 'catch (...)'
};

struct SwitchCase {
  bool isDefault = false;
  ExprPtr value;          // case value expression
  std::vector<StmtPtr> stmts;
};

struct Stmt {
  enum class K {
    Block, VarDecl, Expr, If, While, Do, For, ForEach, Switch, Case,
    Break, Continue, Return, Throw, Try, Empty, Label
  };
  K k = K::Empty;

  // Block
  BlockPtr block;

  // VarDecl
  VarDeclS vdecl;

  // Expr
  ExprPtr expr;

  // If / While / Do
  ExprPtr cond;
  StmtPtr thenS;
  StmtPtr elseS;          // If only

  // For
  VarDeclS forInitV;      // valid if hasForInitV
  bool hasForInitV = false;
  ExprPtr forInitE;       // valid if !hasForInitV && forInitE != null
  ExprPtr forCond;
  ExprPtr forInc;
  StmtPtr body;

  // ForEach
  TypePtr eachType;
  std::string eachName;
  ExprPtr eachColl;

  // Switch
  ExprPtr swCond;
  std::vector<SwitchCase> swCases;

  // Return / Throw
  bool hasValue = false;

  // Try
  BlockPtr tryBlock;
  std::vector<CatchClause> catches;
  BlockPtr finallyBlock;

  // Label (C++ goto labels only; mapped away)
  std::string label;
  StmtPtr labelBody;

  int line = 0;
};

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------
struct Method {
  std::string name;
  TypePtr ret = nullptr;          // null for constructors/destructors/void handled by ret=void Type
  bool isCtor = false;
  bool isDtor = false;
  std::vector<std::string> mods;  // source keyword modifiers in order
  std::vector<std::string> typeParams; // method-level generics <T,...>
  std::vector<Param> params;
  std::vector<TypePtr> throws;
  BlockPtr body = nullptr;        // null => declaration only (abstract / interface / pure virtual)
  std::vector<std::pair<std::string, ExprPtr>> initList; // C++ ctor init list
  int line = 0;
};

struct Field {
  std::vector<std::string> mods;
  TypePtr type;
  std::string name;
  ExprPtr init = nullptr;
  int line = 0;
};

struct BaseSpec {
  TypePtr type;               // base class type (name + optional template args)
  bool isPublic = true;
  bool isVirtual = false;
};

struct ClassDecl {
  std::string name;
  std::vector<std::string> mods;
  // Enclosing C++ namespace path (empty at file scope). Java has no
  // namespaces, so the generator uses it to qualify references to the
  // namespace's holder-class members.
  std::vector<std::string> ns;
  bool isInterface = false;
  bool isStruct = false;
  std::vector<std::string> typeParams;
  std::vector<BaseSpec> bases;    // extends/implements or C++ bases in order
  std::vector<Field> fields;
  std::vector<Method> methods;
  int line = 0;
};

struct EnumDecl {
  std::string name;
  std::vector<std::string> mods;
  bool scoped = false;            // C++ enum class
  std::vector<std::string> values;
  int line = 0;
};

// ---------------------------------------------------------------------------
// Program (parse result)
// ---------------------------------------------------------------------------
// Items declared inside a *named* C++ namespace body. Java has no namespace
// concept, so each bucket is emitted as a holder class named after the
// namespace (last path segment).
struct NamespaceItems {
  std::vector<std::string> parts;                        // e.g. {"YXStyle"}
  std::vector<Field> globals;
  std::vector<std::shared_ptr<Method>> freeFuncs;
};

struct Program {
  std::vector<std::string> packageParts;  // package a.b / namespace a::b
  std::vector<std::string> imports;       // Java import lines (as written, w/o ';')
  std::vector<std::string> includes;      // raw '#include ...' lines
  std::vector<std::string> usingNs;       // 'using namespace X;'
  std::vector<std::shared_ptr<ClassDecl>> classes;
  std::vector<std::shared_ptr<EnumDecl>> enums;
  std::vector<std::shared_ptr<Method>> freeFuncs; // top-level C++ functions
  std::vector<Field> globals;                    // top-level C++ variables
  std::vector<NamespaceItems> namespaces;        // named namespace bodies
  std::vector<Diag> diags;                // warnings collected during parse
};

// ---------------------------------------------------------------------------
// Code writer with indentation
// ---------------------------------------------------------------------------
class Out {
public:
  std::ostringstream os;
  int indent = 0;
  std::string pad() const { return std::string(static_cast<size_t>(indent) * 2u, ' '); }
  void line(const std::string& s = "") { os << pad() << s << "\n"; }
  void raw(const std::string& s) { os << s; }
  void open(const std::string& hdr) { line(hdr + " {"); ++indent; }
  void closeSemi() { --indent; line("};"); }
  void closePlain() { --indent; line("}"); }
  std::string str() const { return os.str(); }
};

// ---------------------------------------------------------------------------
// Small string utilities (ASCII-safe helpers)
// ---------------------------------------------------------------------------
std::string trimWs(const std::string& s);
std::vector<std::string> splitStr(const std::string& s, char sep);
std::string joinStr(const std::vector<std::string>& parts, const std::string& sep);
bool startsWith(const std::string& s, const std::string& p);
bool endsWith(const std::string& s, const std::string& p);
std::string toLower(const std::string& s);
std::string replaceAll(std::string s, const std::string& from, const std::string& to);

// ---------------------------------------------------------------------------
// Lexer + Parser entry points
// ---------------------------------------------------------------------------
std::vector<Token> lex(const std::string& text);

// Result of the C++ preprocessor pass (macro expansion + conditional
// compilation). 'text' keeps the original line numbering (processed lines are
// blanked, not removed) so diagnostics still point at real source lines.
struct PpResult {
  std::string text;
  std::vector<std::string> includes;  // '#include' targets, as written
};
PpResult preprocessCpp(const std::string& text, std::vector<Diag>& diags);

std::shared_ptr<Program> parse(const std::string& text, std::vector<Diag>& diags);

// ---------------------------------------------------------------------------
// Analysis: name guards + type mapping + symbol tables (analyze.cpp)
// ---------------------------------------------------------------------------
bool isCppReserved(const std::string& s);
bool isJavaReserved(const std::string& s);
std::string sanitizeCppName(const std::string& s);
std::string sanitizeJavaName(const std::string& s);
bool isJavaBuiltin(const std::string& s);
bool isJavaPrimitive(const std::string& s);

// Type mapping. 'typeParams' lists generic/template placeholders of the
// current enclosing scope (e.g. {"T"}); placeholders pass through unchanged.
std::string mapJ2C(const Type& t, const std::vector<std::string>& typeParams);
std::string mapC2J(const Type& t, const std::vector<std::string>& typeParams);

// A callable method signature (name + parameter types + owner), used for
// overload resolution across inheritance chains.
struct MethodSig {
  std::string name;
  std::vector<TypePtr> params;
  Method* m = nullptr;      // points into Program-owned storage
  std::string owner;        // declaring class name
};

// Per-class symbol table: own + inherited fields/methods.
struct ClassInfo {
  std::string name;
  bool isInterface = false;
  bool isStruct = false;
  std::vector<std::string> typeParams;
  std::vector<BaseSpec> bases;
  std::vector<Field> fields;      // own fields (copies)
  std::vector<Method> methods;    // own methods (copies)
  std::map<std::string, TypePtr> ownFields;
  std::map<std::string, TypePtr> allFields;   // including inherited
  std::vector<MethodSig> ownMethods;
  std::vector<MethodSig> allMethods;          // including inherited
};

using ClassMap = std::map<std::string, ClassInfo>;

ClassMap buildSymbols(const Program& p, std::vector<Diag>& diags);
const ClassInfo* findClass(const ClassMap& cm, const std::string& name);
TypePtr lookupField(const ClassMap& cm, const std::string& className,
                    const std::string& fieldName);
// Best-match overload lookup by name + argument count + argument types.
const Method* lookupMethod(const ClassMap& cm, const std::string& className,
                           const std::string& name, size_t argc,
                           const std::vector<TypePtr>& argTypes,
                           std::string* outOwner = nullptr);

// Stacked local-variable scope used by code generators.
class Scope {
public:
  void push();
  void pop();
  void add(const std::string& name, const TypePtr& t);
  TypePtr lookup(const std::string& name) const;
  bool has(const std::string& name) const;
private:
  std::vector<std::map<std::string, TypePtr>> frames_;
};

// ---------------------------------------------------------------------------
// Code generation (gen_common.cpp / gen_j2c.cpp / gen_c2j.cpp)
// ---------------------------------------------------------------------------
// Per-translation context shared by both generators.
struct GenCtx {
  const Program* prog = nullptr;
  ClassMap syms;                       // symbol tables
  std::string curClass;                // enclosing class ("" at top level)
  std::vector<std::string> typeParams; // enclosing generic/template params
  Scope locals;                        // params + local variables
  TypePtr retType = nullptr;           // return type of the method being emitted
  Out* mainOut = nullptr;              // sink for a top-level main() (class-outside)
  // enum constant -> owning enum name (Java requires 'Enum.CONST')
  std::map<std::string, std::string> enumConstOwner;
  std::set<std::string> enumNames;     // every enum type name
  // C++ namespace holder class -> members declared in it (globals + free
  // functions). Used to qualify bare references from inside that namespace.
  std::map<std::string, std::set<std::string>> nsMembers;
  std::vector<Diag>& diags;

  explicit GenCtx(std::vector<Diag>& d) : diags(d) {}
  void warn(int line, const std::string& msg);
};

// Best-effort Java-side expression type inference (used for string
// concatenation rewrites). Returns nullptr when unknown.
TypePtr inferJavaType(const ExprPtr& e, const GenCtx& ctx);

// Escape a string literal payload for C++/Java output (both use the same
// core escapes; inner text is verbatim from the lexer).
std::string litEscape(const std::string& inner);

std::string generateJ2C(const std::shared_ptr<Program>& p, std::vector<Diag>& diags);
std::string generateC2J(const std::shared_ptr<Program>& p, std::vector<Diag>& diags);

} // namespace bridge