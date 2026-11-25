// JavaParser.h - 自包含 Java 解析器
#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <regex>
#include <fstream>
#include <sstream>
#include <algorithm>

struct JavaClassInfo {
    std::string name;
    std::string package;
    std::string superClass;
    std::vector<std::string> interfaces;
    std::vector<std::string> annotations;
    std::set<std::string> modifiers;
    std::vector<std::string> fieldNames;
    std::vector<std::string> methodNames;
    bool isInterface = false;
    bool isEnum = false;
    bool isAbstract = false;
    bool isFinal = false;
};

struct JavaMethodInfo {
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> parameters;
    std::vector<std::string> annotations;
    std::string body;
    bool isStatic = false;
    bool isAbstract = false;
};

struct JavaFieldInfo {
    std::string name;
    std::string type;
    std::vector<std::string> annotations;
    bool isStatic = false;
    bool isFinal = false;
};

struct JavaCompilationUnit {
    std::string package;
    std::vector<std::string> imports;
    std::vector<JavaClassInfo> classes;
    std::vector<JavaMethodInfo> methods;
    std::vector<JavaFieldInfo> fields;
    std::string sourceFile;
    std::string fullSource;
    size_t tokenCount = 0;
    size_t lineCount = 0;
};

class JavaParser {
public:
    static JavaCompilationUnit Parse(const std::string& source, const std::string& fileName = "");
    static JavaCompilationUnit ParseFile(const std::string& filePath);
    static std::vector<JavaCompilationUnit> ParseDirectory(const std::string& dirPath);

private:
    static void ExtractPackage(JavaCompilationUnit& unit, const std::string& source);
    static void ExtractImports(JavaCompilationUnit& unit, const std::string& source);
    static void ExtractClasses(JavaCompilationUnit& unit, const std::string& source);
    static void ExtractMethods(JavaCompilationUnit& unit, const std::string& source);
    static void ExtractFields(JavaCompilationUnit& unit, const std::string& source);
    static void ExtractAnnotations(JavaCompilationUnit& unit, const std::string& source);
    static void ExtractModifiers(JavaClassInfo& cls, const std::string& classDecl);
    static std::string Trim(const std::string& str);
    static std::string GetBetween(const std::string& str, const std::string& start, const std::string& end);
};