// LibraryScanner.h
// 库扫描：在用户指定的库文件内查找可用代码（函数），
// 并生成将其翻译为指定语言的「库名称.json」。
#pragma once
#include <string>
#include <vector>
#include <map>
#include "Translator.h"

namespace vcb {

class LibraryScanner {
public:
    struct Function {
        std::string name;
        std::string returnType;
        std::string params;
        std::string signature;   // 完整签名
        std::string comment;     // 前置注释（英文原文）
    };

    struct Result {
        std::string library;
        std::string language;
        std::string locale;
        std::vector<Function> functions;
        std::map<std::string, std::string> translations; // name -> 译文
    };

    // 扫描源码，libraryName = 库名称，language/locale = 目标语言
    static bool Scan(const std::string& code, const std::string& libraryName,
                     const std::string& language, const std::string& locale,
                     Result& out, std::string& err);

    // 生成「库名称.json」内容
    static std::string BuildTranslationJson(const Result& r);

    // 把整句/注释做“逐词”翻译（尽力而为，未知词保留英文）
    static std::string TranslateSentence(const std::string& locale, const std::string& text);
};

} // namespace vcb
