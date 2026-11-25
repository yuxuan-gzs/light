// JavaParser.cpp
#include "JavaParser.h"
#include <filesystem>
#include <regex>
#include <chrono>

namespace fs = std::filesystem;

std::string JavaParser::Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r\f\v");
    return str.substr(first, last - first + 1);
}

std::string JavaParser::GetBetween(const std::string& str, const std::string& start, const std::string& end) {
    size_t startPos = str.find(start);
    if (startPos == std::string::npos) return "";
    startPos += start.length();
    size_t endPos = str.find(end, startPos);
    if (endPos == std::string::npos) return "";
    return str.substr(startPos, endPos - startPos);
}

void JavaParser::ExtractPackage(JavaCompilationUnit& unit, const std::string& source) {
    std::regex packageRegex(R"(package\s+([\w.]+)\s*;)");
    std::smatch match;
    if (std::regex_search(source, match, packageRegex) && match.size() > 1) {
        unit.package = match[1].str();
    }
}

void JavaParser::ExtractImports(JavaCompilationUnit& unit, const std::string& source) {
    std::regex importRegex(R"(import\s+(?:static\s+)?([\w.*]+)\s*;)");
    auto begin = std::sregex_iterator(source.begin(), source.end(), importRegex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        unit.imports.push_back((*it)[1].str());
    }
}

void JavaParser::ExtractModifiers(JavaClassInfo& cls, const std::string& classDecl) {
    std::string decl = classDecl;
    if (decl.find("abstract") != std::string::npos) cls.isAbstract = true;
    if (decl.find("final") != std::string::npos) cls.isFinal = true;
    if (decl.find("static") != std::string::npos) cls.modifiers.insert("static");
    if (decl.find("public") != std::string::npos) cls.modifiers.insert("public");
    if (decl.find("private") != std::string::npos) cls.modifiers.insert("private");
    if (decl.find("protected") != std::string::npos) cls.modifiers.insert("protected");
}

void JavaParser::ExtractClasses(JavaCompilationUnit& unit, const std::string& source) {
    // 提取类
    std::regex classRegex(
        R"((?:public\s+|private\s+|protected\s+|abstract\s+|final\s+|static\s+)*class\s+(\w+)\s*(?:<[^>]+>)?\s*(?:extends\s+(\w+))?\s*(?:implements\s+([\w,\s]+))?\s*\{)"
    );
    auto begin = std::sregex_iterator(source.begin(), source.end(), classRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        JavaClassInfo cls;
        cls.name = (*it)[1].str();
        if ((*it).size() > 2 && !(*it)[2].str().empty()) {
            cls.superClass = (*it)[2].str();
        }
        if ((*it).size() > 3 && !(*it)[3].str().empty()) {
            std::string interfacesStr = (*it)[3].str();
            std::regex ifaceRegex(R"(\w+)");
            auto iBegin = std::sregex_iterator(interfacesStr.begin(), interfacesStr.end(), ifaceRegex);
            auto iEnd = std::sregex_iterator();
            for (auto iIt = iBegin; iIt != iEnd; ++iIt) {
                cls.interfaces.push_back(iIt->str());
            }
        }
        ExtractModifiers(cls, (*it).str());

        // 提取类体中的字段和方法名
        std::string classBody = GetBetween((*it).str(), "{", "}");
        if (!classBody.empty()) {
            // 提取字段
            std::regex fieldRegex(R"((?:public|private|protected|static|final)\s+(\w+(?:<[^>]+>)?)\s+(\w+)\s*(?:=\s*[^;]+)?;)");
            auto fBegin = std::sregex_iterator(classBody.begin(), classBody.end(), fieldRegex);
            auto fEnd = std::sregex_iterator();
            for (auto fIt = fBegin; fIt != fEnd; ++fIt) {
                cls.fieldNames.push_back((*fIt)[2].str());
            }

            // 提取方法名
            std::regex methodRegex(R"((?:public|private|protected|static|final|abstract)\s+\w+(?:<[^>]+>)?\s+(\w+)\s*\()");
            auto mBegin = std::sregex_iterator(classBody.begin(), classBody.end(), methodRegex);
            auto mEnd = std::sregex_iterator();
            for (auto mIt = mBegin; mIt != mEnd; ++mIt) {
                cls.methodNames.push_back((*mIt)[1].str());
            }
        }

        unit.classes.push_back(cls);
    }

    // 提取接口
    std::regex interfaceRegex(R"((?:public\s+|private\s+|protected\s+)?interface\s+(\w+)\s*(?:<[^>]+>)?\s*(?:extends\s+([\w,\s]+))?\s*\{)");
    auto iBegin2 = std::sregex_iterator(source.begin(), source.end(), interfaceRegex);
    auto iEnd2 = std::sregex_iterator();
    for (auto it = iBegin2; it != iEnd2; ++it) {
        JavaClassInfo cls;
        cls.name = (*it)[1].str();
        cls.isInterface = true;
        unit.classes.push_back(cls);
    }

    // 提取枚举
    std::regex enumRegex(R"((?:public\s+|private\s+|protected\s+)?enum\s+(\w+)\s*(?:implements\s+([\w,\s]+))?\s*\{)");
    auto eBegin = std::sregex_iterator(source.begin(), source.end(), enumRegex);
    auto eEnd = std::sregex_iterator();
    for (auto it = eBegin; it != eEnd; ++it) {
        JavaClassInfo cls;
        cls.name = (*it)[1].str();
        cls.isEnum = true;
        unit.classes.push_back(cls);
    }
}

void JavaParser::ExtractAnnotations(JavaCompilationUnit& unit, const std::string& source) {
    std::regex annRegex(R"(@(\w+)(?:\([^)]*\))?)");
    auto begin = std::sregex_iterator(source.begin(), source.end(), annRegex);
    auto end = std::sregex_iterator();
    for (auto it = begin; it != end; ++it) {
        if (!unit.classes.empty()) {
            unit.classes.back().annotations.push_back((*it)[1].str());
        }
    }
}

void JavaParser::ExtractMethods(JavaCompilationUnit& unit, const std::string& source) {
    // 匹配完整方法声明
    std::regex methodRegex(
        R"((?:public|private|protected)\s+(?:static\s+)?(?:final\s+)?(?:synchronized\s+)?(?:abstract\s+)?(\w+(?:<[^>]+>)?)\s+(\w+)\s*\(([^)]*)\)\s*(?:throws\s+[\w,\s]+)?\s*(\{|\;))"
    );
    auto begin = std::sregex_iterator(source.begin(), source.end(), methodRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        JavaMethodInfo method;
        method.returnType = (*it)[1].str();
        method.name = (*it)[2].str();

        std::string paramStr = (*it)[3].str();
        if (!paramStr.empty()) {
            std::regex paramRegex(R"((\w+(?:<[^>]+>)?)\s+(\w+))");
            auto pBegin = std::sregex_iterator(paramStr.begin(), paramStr.end(), paramRegex);
            auto pEnd = std::sregex_iterator();
            for (auto pIt = pBegin; pIt != pEnd; ++pIt) {
                method.parameters.push_back({ (*pIt)[1].str(), (*pIt)[2].str() });
            }
        }

        // 检查是否有方法体
        if ((*it)[4].str() == "{") {
            // 提取方法体
            std::string fullMatch = (*it).str();
            std::string body = GetBetween(fullMatch, "{", "}");
            method.body = body;
        }
        else {
            method.body = ";";
            method.isAbstract = true;
        }

        unit.methods.push_back(method);
    }
}

void JavaParser::ExtractFields(JavaCompilationUnit& unit, const std::string& source) {
    std::regex fieldRegex(R"((?:public|private|protected)\s+(?:static\s+)?(?:final\s+)?(\w+(?:<[^>]+>)?)\s+(\w+)\s*(?:=\s*[^;]+)?;)");
    auto begin = std::sregex_iterator(source.begin(), source.end(), fieldRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        // 排除方法内的局部变量（简化：不处理方法体）
        JavaFieldInfo field;
        field.type = (*it)[1].str();
        field.name = (*it)[2].str();
        unit.fields.push_back(field);
    }
}

JavaCompilationUnit JavaParser::Parse(const std::string& source, const std::string& fileName) {
    auto startTime = std::chrono::high_resolution_clock::now();

    JavaCompilationUnit unit;
    unit.sourceFile = fileName;
    unit.fullSource = source;

    // 统计行数
    unit.lineCount = std::count(source.begin(), source.end(), '\n') + 1;

    // 统计 token 数
    std::regex tokenRegex(R"(\w+|[{}()\[\];,.\+\-\*/%&=!<>|^~?:])");
    unit.tokenCount = std::distance(
        std::sregex_iterator(source.begin(), source.end(), tokenRegex),
        std::sregex_iterator()
    );

    // 提取信息
    ExtractPackage(unit, source);
    ExtractImports(unit, source);
    ExtractClasses(unit, source);
    ExtractAnnotations(unit, source);
    ExtractMethods(unit, source);
    ExtractFields(unit, source);

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    return unit;
}

JavaCompilationUnit JavaParser::ParseFile(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        JavaCompilationUnit empty;
        empty.sourceFile = filePath;
        return empty;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    file.close();
    return Parse(source, filePath);
}

std::vector<JavaCompilationUnit> JavaParser::ParseDirectory(const std::string& dirPath) {
    std::vector<JavaCompilationUnit> results;
    try {
        for (auto& entry : fs::recursive_directory_iterator(dirPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".java") {
                results.push_back(ParseFile(entry.path().string()));
            }
        }
    }
    catch (const std::exception& e) {
        // ignore
    }
    return results;
}