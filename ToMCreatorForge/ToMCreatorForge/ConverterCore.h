// ConverterCore.h
#pragma once
#include <string>
#include <vector>
#include <functional>

// ========== 转换选项结构 ==========
struct ConvertOptions {
    bool generateEvents = true;
    bool generateCaps = true;
    bool generateNetwork = true;
    bool exportZip = true;
    bool includeSource = false;
    bool generateDocs = false;
};

// ========== 注册物品信息 ==========
struct RegisteredItem {
    std::string name;
    std::string className;
    std::string registryName;
    bool isBlock;
    bool hasModel;
    RegisteredItem() : isBlock(false), hasModel(true) {}
};

// ========== 注册配方信息 ==========
struct RegisteredRecipe {
    std::string name;
    std::string type;
    std::string output;
    int outputCount;
    std::vector<std::string> ingredients;
    RegisteredRecipe() : outputCount(1) {}
};

// ========== 事件信息 ==========
struct ModEvent {
    std::string eventClass;
    std::string methodName;
    std::string eventType;
    std::vector<std::string> parameters;
    bool isCancelable;
    ModEvent() : isCancelable(false) {}
};

// ========== Capability 信息 ==========
struct CapabilityData {
    std::string capabilityName;
    std::string interfaceClass;
    std::string implementationClass;
    bool isAttachableToEntity;
    bool isAttachableToItem;
    CapabilityData() : isAttachableToEntity(false), isAttachableToItem(false) {}
};

// ========== 转换核心类 ==========
class ConverterCore {
public:
    using LogCallback = std::function<void(const std::string&)>;
    using ProgressCallback = std::function<void(int)>;

    bool Convert(const std::string& sourcePath,
        const std::string& outputPath,
        LogCallback logCb,
        ProgressCallback progressCb,
        const ConvertOptions& options = ConvertOptions());
};