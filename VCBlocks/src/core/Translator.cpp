// Translator.cpp
#include "Translator.h"
#include "util.h"

namespace vcb {

void Translator::Clear() {
    library.clear();
    language.clear();
    locale.clear();
    strings.clear();
}

bool Translator::LoadFromFile(const std::wstring& path, std::string& err) {
    std::string text;
    if (!ReadFileText(path, text)) { err = "cannot read file"; return false; }
    JsonValue root;
    if (!ParseJson(text, root, err)) return false;
    return LoadFromJson(root, err);
}

bool Translator::LoadFromJson(const JsonValue& root, std::string& err) {
    if (!root.IsObject()) { err = "translation root must be object"; return false; }
    Clear();
    library = root.Get("name").AsString();
    language = root.Get("language").AsString();
    locale = root.Get("locale").AsString();
    if (root.Has("strings") && root.Get("strings").IsObject()) {
        for (const auto& kv : root.Get("strings").obj) {
            strings[kv.first] = kv.second.AsString();
        }
    }
    return true;
}

void Translator::SaveToFile(const std::wstring& path) const {
    WriteFileText(path, ToJsonString());
}

std::string Translator::ToJsonString() const {
    JsonValue root = JsonValue::MakeObject();
    root.Set("name", JsonValue::MakeString(library));
    root.Set("language", JsonValue::MakeString(language));
    root.Set("locale", JsonValue::MakeString(locale));
    JsonValue s = JsonValue::MakeObject();
    for (const auto& kv : strings) s.Set(kv.first, JsonValue::MakeString(kv.second));
    root.Set("strings", s);
    return DumpJson(root);
}

std::string Translator::Translate(const std::string& english) const {
    auto it = strings.find(english);
    if (it != strings.end() && !it->second.empty()) return it->second;
    const char* b = BuiltinString(locale, english);
    if (b && b[0]) return b;
    return english; // 回退英文
}

std::wstring Translator::TranslateW(const std::string& english) const {
    return Utf8ToWide(Translate(english));
}

std::vector<LocaleInfo> Translator::SupportedLocales() {
    return {
        { "en",    "English" },
        { "zh-CN", "简体中文" },
        { "ja",    "日本語" },
        { "fr",    "Français" },
        { "de",    "Deutsch" },
        { "es",    "Español" },
    };
}

const LocaleInfo* Translator::FindLocale(const std::string& locale) {
    for (const auto& l : SupportedLocales()) if (l.locale == locale) return &l;
    return nullptr;
}

// ============================================================
// 内置块标签/分类词典
// ============================================================

static const char* ZhString(const std::string& e) {
    static const std::map<std::string, std::string> m = {
        { "Output", "输出" }, { "Variables", "变量" }, { "Control", "控制" },
        { "Math", "数学" }, { "Logic", "逻辑" },
        { "print {0}", "打印 {0}" },
        { "print number {0}", "打印数字 {0}" },
        { "set {0} to {1}", "将 {0} 设为 {1}" },
        { "change {0} by {1}", "将 {0} 增加 {1}" },
        { "if {0} then", "如果 {0} 那么" },
        { "if {0} then / else", "如果 {0} 那么 否则" },
        { "repeat {0} times", "重复 {0} 次" },
        { "while {0}", "当 {0} 时" },
        { "number", "数字" },
        { "pick random {0} to {1}", "在 {0} 到 {1} 之间取随机数" },
        { "{0} and {1}", "{0} 且 {1}" },
        { "{0} or {1}", "{0} 或 {1}" },
        { "not {0}", "非 {0}" },
        { "else", "否则" },
    };
    auto it = m.find(e);
    return it == m.end() ? nullptr : it->second.c_str();
}

static const char* JaString(const std::string& e) {
    static const std::map<std::string, std::string> m = {
        { "Output", "出力" }, { "Variables", "変数" }, { "Control", "制御" },
        { "Math", "演算" }, { "Logic", "論理" },
        { "print {0}", "{0} と表示" },
        { "if {0} then", "もし {0} なら" },
        { "repeat {0} times", "{0} 回繰り返す" },
        { "while {0}", "{0} の間" },
        { "else", "それ以外" },
    };
    auto it = m.find(e);
    return it == m.end() ? nullptr : it->second.c_str();
}

static const char* FrString(const std::string& e) {
    static const std::map<std::string, std::string> m = {
        { "Output", "Sortie" }, { "Variables", "Variables" }, { "Control", "Contrôle" },
        { "Math", "Maths" }, { "Logic", "Logique" },
        { "print {0}", "afficher {0}" },
        { "if {0} then", "si {0} alors" },
        { "repeat {0} times", "répéter {0} fois" },
        { "else", "sinon" },
    };
    auto it = m.find(e);
    return it == m.end() ? nullptr : it->second.c_str();
}

static const char* DeString(const std::string& e) {
    static const std::map<std::string, std::string> m = {
        { "Output", "Ausgabe" }, { "Variables", "Variablen" }, { "Control", "Steuerung" },
        { "Math", "Mathematik" }, { "Logic", "Logik" },
        { "print {0}", "gebe {0} aus" },
        { "if {0} then", "falls {0}, dann" },
        { "else", "sonst" },
    };
    auto it = m.find(e);
    return it == m.end() ? nullptr : it->second.c_str();
}

static const char* EsString(const std::string& e) {
    static const std::map<std::string, std::string> m = {
        { "Output", "Salida" }, { "Variables", "Variables" }, { "Control", "Control" },
        { "Math", "Matemáticas" }, { "Logic", "Lógica" },
        { "print {0}", "mostrar {0}" },
        { "if {0} then", "si {0} entonces" },
        { "else", "si no" },
    };
    auto it = m.find(e);
    return it == m.end() ? nullptr : it->second.c_str();
}

const char* Translator::BuiltinString(const std::string& locale, const std::string& english) {
    if (locale == "zh-CN" || locale == "zh") return ZhString(english);
    if (locale == "ja") return JaString(english);
    if (locale == "fr") return FrString(english);
    if (locale == "de") return DeString(english);
    if (locale == "es") return EsString(english);
    return nullptr;
}

// ============================================================
// 内置单词词典（库扫描翻译标识符用）
// ============================================================

static const char* ZhWord(const std::string& w) {
    static const std::map<std::string, std::string> m = {
        { "get", "获取" }, { "set", "设置" }, { "print", "打印" }, { "printf", "打印" },
        { "open", "打开" }, { "close", "关闭" }, { "read", "读取" }, { "write", "写入" },
        { "find", "查找" }, { "create", "创建" }, { "delete", "删除" }, { "remove", "移除" },
        { "init", "初始化" }, { "free", "释放" }, { "alloc", "分配" }, { "malloc", "分配内存" },
        { "copy", "复制" }, { "move", "移动" }, { "add", "添加" }, { "insert", "插入" },
        { "start", "启动" }, { "stop", "停止" }, { "run", "运行" }, { "load", "加载" },
        { "save", "保存" }, { "update", "更新" }, { "check", "检查" }, { "is", "是否" },
        { "has", "是否拥有" }, { "size", "大小" }, { "length", "长度" }, { "count", "计数" },
        { "file", "文件" }, { "image", "图像" }, { "text", "文本" }, { "data", "数据" },
        { "buffer", "缓冲区" }, { "string", "字符串" }, { "list", "列表" }, { "array", "数组" },
        { "config", "配置" }, { "parse", "解析" }, { "format", "格式化" }, { "convert", "转换" },
        { "compute", "计算" }, { "calc", "计算" }, { "process", "处理" }, { "sort", "排序" },
        { "search", "搜索" }, { "merge", "合并" }, { "split", "分割" }, { "join", "连接" },
        { "encode", "编码" }, { "decode", "解码" }, { "hash", "哈希" }, { "encrypt", "加密" },
        { "decrypt", "解密" }, { "compress", "压缩" }, { "decompress", "解压" },
        { "begin", "开始" }, { "end", "结束" }, { "main", "主" }, { "test", "测试" },
        { "render", "渲染" }, { "draw", "绘制" }, { "clear", "清除" }, { "reset", "重置" },
        { "push", "压入" }, { "pop", "弹出" }, { "peek", "窥视" }, { "empty", "空" },
        { "min", "最小" }, { "max", "最大" }, { "sum", "求和" }, { "average", "平均" },
        { "value", "值" }, { "result", "结果" }, { "error", "错误" }, { "debug", "调试" },
        { "log", "日志" }, { "input", "输入" }, { "output", "输出" },
    };
    auto it = m.find(w);
    return it == m.end() ? nullptr : it->second.c_str();
}

static const char* JaWord(const std::string& w) {
    static const std::map<std::string, std::string> m = {
        { "get", "取得" }, { "set", "設定" }, { "print", "表示" }, { "open", "開く" },
        { "close", "閉じる" }, { "read", "読む" }, { "write", "書く" }, { "find", "検索" },
        { "create", "作成" }, { "delete", "削除" }, { "init", "初期化" }, { "copy", "複製" },
        { "move", "移動" }, { "start", "開始" }, { "stop", "停止" }, { "run", "実行" },
        { "load", "読み込み" }, { "save", "保存" }, { "size", "サイズ" }, { "file", "ファイル" },
    };
    auto it = m.find(w);
    return it == m.end() ? nullptr : it->second.c_str();
}

static const char* FrWord(const std::string& w) {
    static const std::map<std::string, std::string> m = {
        { "get", "obtenir" }, { "set", "définir" }, { "print", "afficher" }, { "open", "ouvrir" },
        { "close", "fermer" }, { "read", "lire" }, { "write", "écrire" }, { "find", "chercher" },
        { "create", "créer" }, { "delete", "supprimer" }, { "init", "initialiser" },
        { "copy", "copier" }, { "move", "déplacer" }, { "start", "démarrer" }, { "stop", "arrêter" },
        { "run", "exécuter" }, { "load", "charger" }, { "save", "enregistrer" }, { "file", "fichier" },
    };
    auto it = m.find(w);
    return it == m.end() ? nullptr : it->second.c_str();
}

static const char* DeWord(const std::string& w) {
    static const std::map<std::string, std::string> m = {
        { "get", "holen" }, { "set", "setzen" }, { "print", "ausgeben" }, { "open", "öffnen" },
        { "close", "schließen" }, { "read", "lesen" }, { "write", "schreiben" }, { "find", "finden" },
        { "create", "erstellen" }, { "delete", "löschen" }, { "init", "initialisieren" },
        { "copy", "kopieren" }, { "move", "verschieben" }, { "start", "starten" }, { "stop", "stoppen" },
        { "load", "laden" }, { "save", "speichern" }, { "file", "Datei" },
    };
    auto it = m.find(w);
    return it == m.end() ? nullptr : it->second.c_str();
}

static const char* EsWord(const std::string& w) {
    static const std::map<std::string, std::string> m = {
        { "get", "obtener" }, { "set", "establecer" }, { "print", "mostrar" }, { "open", "abrir" },
        { "close", "cerrar" }, { "read", "leer" }, { "write", "escribir" }, { "find", "buscar" },
        { "create", "crear" }, { "delete", "eliminar" }, { "init", "inicializar" },
        { "copy", "copiar" }, { "move", "mover" }, { "start", "iniciar" }, { "stop", "detener" },
        { "load", "cargar" }, { "save", "guardar" }, { "file", "archivo" },
    };
    auto it = m.find(w);
    return it == m.end() ? nullptr : it->second.c_str();
}

const char* Translator::BuiltinWord(const std::string& locale, const std::string& word) {
    if (locale == "en") return word.c_str();
    if (locale == "zh-CN" || locale == "zh") return ZhWord(word);
    if (locale == "ja") return JaWord(word);
    if (locale == "fr") return FrWord(word);
    if (locale == "de") return DeWord(word);
    if (locale == "es") return EsWord(word);
    return nullptr;
}

// 拆词：snake_case / camelCase / 大小写边界
static std::vector<std::string> SplitIdentifier(const std::string& id) {
    std::vector<std::string> words;
    std::string cur;
    auto flush = [&]() {
        if (!cur.empty()) { words.push_back(ToLowerAscii(cur)); cur.clear(); }
    };
    for (size_t i = 0; i < id.size(); ++i) {
        char c = id[i];
        if (c == '_' || c == '-' || c == ' ') { flush(); continue; }
        if (i > 0 && (c >= 'A' && c <= 'Z') && (cur.back() >= 'a' && cur.back() <= 'z')) flush();
        cur += c;
    }
    flush();
    return words;
}

std::string Translator::TranslateIdentifier(const std::string& locale, const std::string& identifier) {
    auto words = SplitIdentifier(identifier);
    bool joinNoSpace = (locale == "zh-CN" || locale == "zh" || locale == "ja");
    std::string out;
    for (const auto& w : words) {
        const char* t = BuiltinWord(locale, w);
        std::string translated = (t && t[0]) ? t : w;
        out += translated;
        if (!joinNoSpace) out += " ";
    }
    if (!joinNoSpace) {
        // 去掉末尾空格
        while (!out.empty() && out.back() == ' ') out.pop_back();
    }
    return out.empty() ? identifier : out;
}

} // namespace vcb
