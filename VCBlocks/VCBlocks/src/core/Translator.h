// Translator.h
// 语言翻译：加载「库名称.json」，将积木英文文字翻译为目标语言，无翻译则回退英文。
#pragma once
#include <string>
#include <vector>
#include <map>
#include "Json.h"

namespace vcb {

struct LocaleInfo {
    std::string locale;   // "en" / "zh-CN" / "ja" ...
    std::string display;  // "English" / "简体中文" ...
};

class Translator {
public:
    std::string library;   // 库名称（文件名主体）
    std::string language;  // 语言显示名
    std::string locale;    // 区域码
    std::map<std::string, std::string> strings; // 英文 -> 译文

    void Clear();
    bool LoadFromFile(const std::wstring& path, std::string& err);
    bool LoadFromJson(const JsonValue& root, std::string& err);
    void SaveToFile(const std::wstring& path) const;
    std::string ToJsonString() const;

    // 翻译：先查加载的 strings，再查内置词典，最后回退英文原文
    std::string Translate(const std::string& english) const;
    std::wstring TranslateW(const std::string& english) const;

    // 内置词典（块标签 + 分类名）
    static const char* BuiltinString(const std::string& locale, const std::string& english);
    // 内置单词词典（用于把标识符翻译成目标语言，供库扫描使用）
    static const char* BuiltinWord(const std::string& locale, const std::string& word);

    // 把 C 标识符（snake_case / camelCase）拆词并翻译后拼接
    static std::string TranslateIdentifier(const std::string& locale, const std::string& identifier);

    static std::vector<LocaleInfo> SupportedLocales();
    static const LocaleInfo* FindLocale(const std::string& locale);
};

} // namespace vcb
