// analyze.cpp - semantic layer (T3): reserved-word guards, Java<->C++ type
// mapping, class symbol tables with inherited member lookup, overload
// resolution helpers, and a stacked local-variable Scope.
#include "bridge.hpp"
#include <map>
#include <set>
#include <algorithm>

namespace bridge {

namespace {

bool contains(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

// Strip a C++ namespace or Java package prefix from a type name.
std::string lastSeg(const std::string& s) {
  size_t p = s.find_last_of("::");
  if (p == std::string::npos) p = s.find_last_of('.');
  return (p == std::string::npos) ? s : s.substr(p + 1);
}

const std::set<std::string>& cppReservedWords() {
  static const std::set<std::string> w = {
    "alignas","alignof","and","and_eq","asm","auto","bitand","bitor","bool",
    "break","case","catch","char","char8_t","char16_t","char32_t","class",
    "compl","concept","const","consteval","constexpr","constinit","const_cast",
    "continue","co_await","co_return","co_yield","decltype","default","delete",
    "do","double","dynamic_cast","else","enum","explicit","export","extern",
    "false","float","for","friend","goto","if","inline","int","long","mutable",
    "namespace","new","noexcept","not","not_eq","nullptr","operator","or",
    "or_eq","private","protected","public","register","reinterpret_cast",
    "requires","return","short","signed","sizeof","static","static_assert",
    "static_cast","struct","switch","template","this","thread_local","throw",
    "true","try","typedef","typeid","typename","union","unsigned","using",
    "virtual","void","volatile","wchar_t","while","xor","xor_eq"
  };
  return w;
}

const std::set<std::string>& javaReservedWords() {
  static const std::set<std::string> w = {
    "abstract","assert","boolean","break","byte","case","catch","char","class",
    "const","continue","default","do","double","else","enum","extends","final",
    "finally","float","for","goto","if","implements","import","instanceof",
    "int","interface","long","native","new","package","private","protected",
    "public","return","short","static","strictfp","super","switch",
    "synchronized","this","throw","throws","transient","try","void","volatile",
    "while","true","false","null","var","record","sealed","permits","yield","_"
  };
  return w;
}

const std::set<std::string>& javaBuiltins() {
  static const std::set<std::string> w = {
    "Object","String","Class","Integer","Long","Short","Byte","Float","Double",
    "Character","Boolean","Void","Number","Math","System","Arrays","Collections",
    "Objects","StringBuilder","StringBuffer","StringJoiner","CharSequence",
    "Comparable","Iterable","Collection","List","ArrayList","LinkedList","Set",
    "HashSet","TreeSet","SortedSet","LinkedHashSet","Map","HashMap","TreeMap",
    "LinkedHashMap","SortedMap","Queue","Deque","ArrayDeque","Stack",
    "PriorityQueue","Optional","OptionalInt","OptionalLong","OptionalDouble",
    "Comparator","Runnable","Callable","Thread","Exception","RuntimeException",
    "IllegalArgumentException","IllegalStateException","NullPointerException",
    "IndexOutOfBoundsException","ArrayIndexOutOfBoundsException",
    "UnsupportedOperationException","Error","Throwable","Date","Calendar",
    "Random","Scanner","PrintStream","InputStream","OutputStream","IOException",
    "File","Path","Files"
  };
  return w;
}

const std::set<std::string>& javaPrimitives() {
  static const std::set<std::string> p = {
    "void","boolean","byte","short","int","long","float","double","char"
  };
  return p;
}

std::string arrSuffix(const Type& t) {
  std::string s;
  for (int d = 0; d < t.dims; ++d) s += "[]";
  return s;
}

const std::map<std::string, std::string>& j2cTypes() {
  static const std::map<std::string, std::string> m = {
    {"void","void"},{"boolean","bool"},{"byte","std::int8_t"},
    {"short","short"},{"int","int"},{"long","long long"},{"float","float"},
    {"double","double"},{"char","char"},
    {"Boolean","bool"},{"Byte","std::int8_t"},{"Short","short"},
    {"Integer","int"},{"Long","long long"},{"Float","float"},{"Double","double"},
    {"Character","char"},
    {"String","std::string"},{"StringBuilder","std::string"},
    {"CharSequence","std::string"},
    {"List","std::vector"},{"ArrayList","std::vector"},
    {"LinkedList","std::vector"},{"Collection","std::vector"},
    {"Iterable","std::vector"},
    {"Set","std::set"},{"HashSet","std::set"},{"TreeSet","std::set"},
    {"SortedSet","std::set"},
    {"Map","std::map"},{"HashMap","std::map"},{"TreeMap","std::map"},
    {"LinkedHashMap","std::map"},{"SortedMap","std::map"},
    {"Optional","std::optional"},
    {"Object","std::any"},
    {"Throwable","std::runtime_error"},{"Error","std::runtime_error"},
    {"Exception","std::runtime_error"},{"RuntimeException","std::runtime_error"},
    {"IllegalArgumentException","std::invalid_argument"},
    {"NumberFormatException","std::invalid_argument"},
    {"IllegalStateException","std::logic_error"},
    {"NullPointerException","std::runtime_error"},
    {"IndexOutOfBoundsException","std::out_of_range"},
    {"ArrayIndexOutOfBoundsException","std::out_of_range"},
    {"UnsupportedOperationException","std::logic_error"}
  };
  return m;
}

const std::map<std::string, std::string>& c2jTypes() {
  static const std::map<std::string, std::string> m = {
    {"void","void"},{"bool","boolean"},{"char","char"},{"wchar_t","char"},
    {"char16_t","char"},{"char32_t","int"},
    {"signed char","byte"},{"unsigned char","byte"},
    {"short","short"},{"unsigned short","int"},
    {"int","int"},{"unsigned int","int"},
    {"long","int"},{"unsigned long","int"},
    {"long long","long"},{"unsigned long long","long"},
    {"float","float"},{"double","double"},{"long double","double"},
    {"std::int8_t","byte"},{"std::int16_t","short"},{"std::int32_t","int"},
    {"std::int64_t","long"},
    {"std::uint8_t","byte"},{"std::uint16_t","int"},{"std::uint32_t","long"},
    {"std::uint64_t","long"},
    {"size_t","long"},{"ptrdiff_t","int"},{"intptr_t","long"},
    {"std::string","String"},{"std::string_view","String"},
    {"std::wstring","String"},{"std::wstring_view","String"},
    {"wstring","String"},{"string_view","String"},{"string","String"},
    {"std::vector","ArrayList"},{"std::list","LinkedList"},
    {"std::deque","ArrayDeque"},
    {"std::set","HashSet"},{"std::unordered_set","HashSet"},
    {"std::multiset","HashSet"},{"std::unordered_multiset","HashSet"},
    {"std::map","HashMap"},{"std::unordered_map","HashMap"},
    {"std::multimap","HashMap"},{"std::unordered_multimap","HashMap"},
    {"std::optional","Optional"},{"std::any","Object"},
    {"std::variant","Object"},{"std::tuple","Object"},
    {"std::function","Object"},{"std::thread","Thread"},
    {"std::mutex","Object"},{"std::recursive_mutex","Object"},
    {"std::condition_variable","Object"},{"std::atomic","Object"},
    {"thread","Thread"},{"mutex","Object"},{"atomic","Object"},
    // Windows typedefs (best effort; handles/pointers become longs)
    {"HANDLE","long"},{"HWND","long"},{"HINSTANCE","long"},{"HMODULE","long"},
    {"HMENU","long"},{"HICON","long"},{"HCURSOR","long"},{"HBRUSH","long"},
    {"HDC","long"},{"HGDIOBJ","long"},{"HFONT","long"},{"HPEN","long"},
    {"HRGN","long"},{"HBITMAP","long"},{"HPALETTE","long"},{"HKEY","long"},
    {"HRSRC","long"},{"HTREEITEM","long"},{"HWINEVENTHOOK","long"},
    {"HRESULT","int"},{"NTSTATUS","int"},{"LRESULT","int"},{"BOOL","boolean"},
    {"BOOLEAN","boolean"},{"DWORD","int"},{"WORD","short"},{"BYTE","byte"},
    {"INT","int"},{"UINT","int"},{"LONG","int"},{"ULONG","long"},
    {"SHORT","short"},{"USHORT","int"},{"LONGLONG","long"},{"ULONGLONG","long"},
    {"WPARAM","long"},{"LPARAM","long"},{"COLORREF","int"},{"ATOM","short"},
    {"LANGID","short"},{"LCID","int"},{"SIZE_T","long"},{"SSIZE_T","long"},
    {"DWORD_PTR","long"},{"UINT_PTR","long"},{"ULONG_PTR","long"},
    {"INT_PTR","long"},{"LONG_PTR","long"},{"WCHAR","char"},{"CHAR","char"},
    {"TCHAR","char"},{"FLOAT","float"},{"DOUBLE","double"},{"VOID","void"},
    {"LPVOID","Object"},{"LPCVOID","Object"},{"PVOID","Object"},
    {"REFIID","Object"},{"REFCLSID","Object"},{"GUID","Object"},{"IID","Object"},
    {"CLSID","Object"},{"VARIANT","Object"},{"BSTR","String"},
    {"LPWSTR","String"},{"LPCWSTR","String"},{"LPSTR","String"},
    {"LPCSTR","String"},{"LPCTSTR","String"},{"LPTSTR","String"}
  };
  return m;
}

void collectInherited(const ClassMap& cm, const ClassInfo& ci,
                      std::set<std::string>& visited) {
  for (auto& b : ci.bases) {
    if (!b.type) continue;
    std::string bn = b.type->base;
    auto it = cm.find(bn);
    if (it == cm.end()) it = cm.find(lastSeg(bn));
    if (it == cm.end()) continue;
    if (!visited.insert(it->first).second) continue;  // cycle / diamond guard
    const ClassInfo& base = it->second;
    for (auto& kv : base.ownFields) const_cast<ClassInfo&>(ci).allFields.emplace(kv.first, kv.second);
    for (auto& ms : base.ownMethods) const_cast<ClassInfo&>(ci).allMethods.push_back(ms);
    collectInherited(cm, base, visited);
  }
}

// Compatibility score for overload resolution: 3 exact, 2 widening, 1 loose,
// 0 incompatible.
int convScore(const Type& from, const Type& to) {
  if (from == to) return 3;
  if (from.args.empty() && to.args.empty()) {
    std::string f = lastSeg(from.base);
    std::string t = lastSeg(to.base);
    if (f == t) return 2;  // same type name, ignore cv/ref spelling
    static const std::map<std::string, std::vector<std::string>> widen = {
      {"char", {"int","long","long long","float","double"}},
      {"byte", {"short","int","long","long long","float","double"}},
      {"std::int8_t", {"short","int","long","long long","float","double"}},
      {"short", {"int","long","long long","float","double"}},
      {"int", {"long","long long","float","double"}},
      {"long", {"long long","float","double"}},
      {"long long", {"float","double"}},
      {"float", {"double"}},
      {"bool", {"int"}}
    };
    auto it = widen.find(f);
    if (it != widen.end()) {
      for (auto& w : it->second) {
        if (w == t) return 2;
      }
    }
    if (t == "Object") return 1;
  } else if (from.args.size() == to.args.size() && !to.args.empty()) {
    int s = 2;
    for (size_t i = 0; i < to.args.size(); ++i) {
      s = std::min(s, convScore(from.args[i], to.args[i]));
      if (s <= 0) break;
    }
    return s;
  }
  return 0;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Reserved-word guards
// ---------------------------------------------------------------------------
bool isCppReserved(const std::string& s) { return cppReservedWords().count(s) > 0; }
bool isJavaReserved(const std::string& s) { return javaReservedWords().count(s) > 0; }
std::string sanitizeCppName(const std::string& s) { return isCppReserved(s) ? s + "_" : s; }
std::string sanitizeJavaName(const std::string& s) { return isJavaReserved(s) ? s + "_" : s; }
bool isJavaBuiltin(const std::string& s) { return javaBuiltins().count(s) > 0; }
bool isJavaPrimitive(const std::string& s) { return javaPrimitives().count(s) > 0; }

// ---------------------------------------------------------------------------
// Type mapping
// ---------------------------------------------------------------------------
std::string mapJ2C(const Type& t, const std::vector<std::string>& typeParams) {
  if (contains(typeParams, t.base)) {
    std::string out = t.base;
    for (int d = 0; d < t.dims; ++d) out = "std::vector<" + out + ">";
    return out;
  }
  std::string b = lastSeg(t.base);
  std::string mapped = b;
  auto it = j2cTypes().find(b);
  if (it != j2cTypes().end()) mapped = it->second;

  std::string out;
  if (t.isUnsigned) out += "unsigned ";
  if (t.isConst) out += "const ";
  out += mapped;
  if (!t.args.empty()) {
    out += "<";
    for (size_t i = 0; i < t.args.size(); ++i) {
      if (i) out += ", ";
      out += mapJ2C(t.args[i], typeParams);
    }
    out += ">";
  }
  for (int d = 0; d < t.dims; ++d) out = "std::vector<" + out + ">";
  return out;
}

std::string boxJavaPrimitive(const std::string& s) {
  if (s == "int") return "Integer";
  if (s == "long") return "Long";
  if (s == "short") return "Short";
  if (s == "byte") return "Byte";
  if (s == "char") return "Character";
  if (s == "float") return "Float";
  if (s == "double") return "Double";
  if (s == "boolean") return "Boolean";
  return s;
}

// box=true when (recursively) mapping a generic/template argument: Java
// type arguments require reference types, so primitives get boxed.
std::string mapC2JImpl(const Type& t, const std::vector<std::string>& typeParams,
                       bool box) {
  if (contains(typeParams, t.base)) return t.base + arrSuffix(t);

  std::string bRaw = t.base;
  std::string b = lastSeg(bRaw);

  // const char* / char* literal-ish pointers map to String.
  if (b == "char" && t.isPtr) {
    return "String" + arrSuffix(t);
  }

  std::string mapped = b;
  auto it = c2jTypes().find(bRaw);
  if (it == c2jTypes().end()) it = c2jTypes().find(b);
  if (it != c2jTypes().end()) mapped = it->second;

  if ((b == "shared_ptr" || b == "unique_ptr" || b == "weak_ptr") && !t.args.empty()) {
    mapped = mapC2JImpl(t.args[0], typeParams, box);
  } else if (b == "atomic" && !t.args.empty()) {
    // std::atomic<T> -> T (the Java memory model has no direct analogue)
    return mapC2JImpl(t.args[0], typeParams, box);
  } else if (!t.args.empty()) {
    if (b == "pair" || b == "tuple" || b == "array" || b == "function" ||
        b == "variant" || b == "queue" || b == "stack" ||
        b == "priority_queue") {
      // Not expressible in Java source: keep a warning-stub type.
      mapped = "Object";
    } else {
      std::string inner;
      for (size_t i = 0; i < t.args.size(); ++i) {
        if (i) inner += ", ";
        inner += mapC2JImpl(t.args[i], typeParams, true);
      }
      mapped += "<" + inner + ">";
    }
  } else if (box) {
    mapped = boxJavaPrimitive(mapped);
  }

  // Fixed-length C++ arrays become Java arrays; raw pointers become Java
  // reference semantics (no array suffix), except 'const char*' already
  // returned as String above.
  int extraDims = t.dims;
  for (int d = 0; d < extraDims; ++d) mapped += "[]";
  return mapped;
}

std::string mapC2J(const Type& t, const std::vector<std::string>& typeParams) {
  return mapC2JImpl(t, typeParams, false);
}

// ---------------------------------------------------------------------------
// Symbol tables
// ---------------------------------------------------------------------------
ClassMap buildSymbols(const Program& p, std::vector<Diag>& diags) {
  (void)diags;
  ClassMap cm;
  for (auto& c : p.classes) {
    if (!c) continue;
    ClassInfo ci;
    ci.name = c->name;
    ci.isInterface = c->isInterface;
    ci.isStruct = c->isStruct;
    ci.typeParams = c->typeParams;
    ci.bases = c->bases;
    for (auto& f : c->fields) {
      ci.fields.push_back(f);
      ci.ownFields.emplace(f.name, f.type);
    }
    ci.methods.reserve(c->methods.size());
    for (size_t i = 0; i < c->methods.size(); ++i) {
      ci.methods.push_back(c->methods[i]);
      MethodSig sig;
      sig.name = c->methods[i].name;
      sig.m = &ci.methods.back();
      sig.owner = c->name;
      for (auto& pr : c->methods[i].params) sig.params.push_back(pr.type);
      ci.ownMethods.push_back(sig);
    }
    cm[c->name] = std::move(ci);
  }
  for (auto& kv : cm) {
    kv.second.allFields = kv.second.ownFields;
    kv.second.allMethods = kv.second.ownMethods;
    std::set<std::string> visited;
    collectInherited(cm, kv.second, visited);
  }
  return cm;
}

const ClassInfo* findClass(const ClassMap& cm, const std::string& name) {
  auto it = cm.find(name);
  if (it != cm.end()) return &it->second;
  std::string last = lastSeg(name);
  it = cm.find(last);
  if (it != cm.end()) return &it->second;
  return nullptr;
}

TypePtr lookupField(const ClassMap& cm, const std::string& className,
                    const std::string& fieldName) {
  const ClassInfo* ci = findClass(cm, className);
  if (!ci) return nullptr;
  auto it = ci->allFields.find(fieldName);
  if (it != ci->allFields.end()) return it->second;
  return nullptr;
}

const Method* lookupMethod(const ClassMap& cm, const std::string& className,
                           const std::string& name, size_t argc,
                           const std::vector<TypePtr>& argTypes,
                           std::string* outOwner) {
  const ClassInfo* ci = findClass(cm, className);
  if (!ci) return nullptr;
  const MethodSig* best = nullptr;
  int bestScore = 0;
  std::string bestOwner;
  for (auto& ms : ci->allMethods) {
    if (ms.name != name) continue;
    if (ms.params.size() != argc) continue;
    int sc = 3;
    for (size_t i = 0; i < argc && sc > 0; ++i) {
      sc = std::min(sc, convScore(*argTypes[i], *ms.params[i]));
    }
    if (sc > bestScore) {
      bestScore = sc;
      best = &ms;
      bestOwner = ms.owner;
    }
  }
  if (best && bestScore > 0) {
    if (outOwner) *outOwner = bestOwner;
    return best->m;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Local scope
// ---------------------------------------------------------------------------
void Scope::push() { frames_.emplace_back(); }
void Scope::pop() {
  if (!frames_.empty()) frames_.pop_back();
}
void Scope::add(const std::string& name, const TypePtr& t) {
  if (frames_.empty()) frames_.emplace_back();
  frames_.back()[name] = t;
}
TypePtr Scope::lookup(const std::string& name) const {
  for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
    auto f = it->find(name);
    if (f != it->end()) return f->second;
  }
  return nullptr;
}
bool Scope::has(const std::string& name) const { return lookup(name) != nullptr; }

} // namespace bridge