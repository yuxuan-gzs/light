// EventParser.h
#pragma once
#include <string>
#include <vector>

struct ModEvent {
    std::string eventClass;
    std::string methodName;
    std::string eventType;
    std::vector<std::string> parameters;
    bool isCancelable;
};

struct CapabilityData {
    std::string capabilityName;
    std::string interfaceClass;
    std::string implementationClass;
    bool isAttachableToEntity;
    bool isAttachableToItem;
};

class EventParser {
public:
    // 解析 @SubscribeEvent 注解
    static std::vector<ModEvent> ParseEvents(const std::string& javaContent);

    // 解析 Capability 注册
    static std::vector<CapabilityData> ParseCapabilities(const std::string& javaContent);

    // 解析网络包注册
    static std::vector<std::string> ParseNetworkPackets(const std::string& javaContent);
};