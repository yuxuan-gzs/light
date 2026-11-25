// EventParser.cpp
#include "EventParser.h"
#include <regex>
#include <sstream>
#include <algorithm>

static std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

static std::string ExtractGenerics(const std::string& str) {
    std::regex genericRegex(R"(<([^>]+)>)");
    std::smatch match;
    if (std::regex_search(str, match, genericRegex) && match.size() > 1) {
        return match[1].str();
    }
    return "";
}

std::vector<ModEvent> EventParser::ParseEvents(const std::string& javaContent) {
    std::vector<ModEvent> events;

    // 1. 查找 @SubscribeEvent 方法
    std::regex eventRegex(R"(@SubscribeEvent\s*(?:\([^)]*\))?\s*(?:public\s+)?(?:static\s+)?(?:void|boolean)\s+(\w+)\s*\(\s*(\w+)\s+(\w+)\s*\))");
    auto begin = std::sregex_iterator(javaContent.begin(), javaContent.end(), eventRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::smatch m = *it;
        if (m.size() >= 4) {
            ModEvent modEvent;
            modEvent.methodName = m[1].str();
            modEvent.eventClass = m[2].str();
            if (modEvent.eventClass.find("Client") != std::string::npos) {
                modEvent.eventType = "Client";
            }
            else if (modEvent.eventClass.find("Server") != std::string::npos) {
                modEvent.eventType = "Server";
            }
            else {
                modEvent.eventType = "Common";
            }
            modEvent.isCancelable = (modEvent.eventClass.find("Cancelable") != std::string::npos);
            modEvent.parameters.push_back(m[2].str() + " " + m[3].str());
            events.push_back(modEvent);
        }
    }

    // 2. 查找更复杂的事件（带泛型）
    std::regex complexEventRegex(R"(@SubscribeEvent\s*(?:\([^)]*\))?\s*(?:public\s+)?(?:static\s+)?(?:void|boolean)\s+(\w+)\s*\(\s*(\w+)(?:<[^>]+>)?\s+(\w+)\s*\))");
    auto cBegin = std::sregex_iterator(javaContent.begin(), javaContent.end(), complexEventRegex);
    auto cEnd = std::sregex_iterator();

    for (auto it = cBegin; it != cEnd; ++it) {
        std::smatch m = *it;
        if (m.size() >= 4) {
            ModEvent modEvent;
            modEvent.methodName = m[1].str();
            modEvent.eventClass = m[2].str();
            modEvent.eventType = "Common";
            modEvent.isCancelable = false;
            modEvent.parameters.push_back(m[2].str() + " " + m[3].str());

            bool exists = false;
            for (auto& e : events) {
                if (e.methodName == modEvent.methodName && e.eventClass == modEvent.eventClass) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                events.push_back(modEvent);
            }
        }
    }

    // 3. 查找 Mod 生命周期事件（@Mod.EventHandler）
    std::regex lifecycleRegex(R"(@Mod\.EventHandler\s*(?:public\s+)?(?:void)\s+(\w+)\s*\(\s*(?:FMLInitializationEvent|FMLPreInitializationEvent|FMLPostInitializationEvent|FMLLoadCompleteEvent)\s+(\w+)\s*\))");
    auto lBegin = std::sregex_iterator(javaContent.begin(), javaContent.end(), lifecycleRegex);
    auto lEnd = std::sregex_iterator();

    for (auto it = lBegin; it != lEnd; ++it) {
        std::smatch m = *it;
        if (m.size() >= 3) {
            ModEvent modEvent;
            modEvent.methodName = m[1].str();
            modEvent.eventClass = m[2].str();
            modEvent.eventType = "FML";
            modEvent.isCancelable = false;

            std::string param = "FMLInitializationEvent event";
            if (modEvent.eventClass == "FMLInitializationEvent") {
                param = "FMLInitializationEvent event";
            }
            else if (modEvent.eventClass == "FMLPreInitializationEvent") {
                param = "FMLPreInitializationEvent event";
            }
            else if (modEvent.eventClass == "FMLPostInitializationEvent") {
                param = "FMLPostInitializationEvent event";
            }
            else if (modEvent.eventClass == "FMLLoadCompleteEvent") {
                param = "FMLLoadCompleteEvent event";
            }
            modEvent.parameters.push_back(param);
            events.push_back(modEvent);
        }
    }

    return events;
}

std::vector<CapabilityData> EventParser::ParseCapabilities(const std::string& javaContent) {
    std::vector<CapabilityData> capabilities;

    std::regex capRegex(R"(@CapabilityInject\s*\(\s*([\w.]+)\.class\s*\)\s*(?:static\s+)?(?:public\s+)?(?:final\s+)?Capability<([^>]+)>\s+(\w+);)");
    auto begin = std::sregex_iterator(javaContent.begin(), javaContent.end(), capRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::smatch m = *it;
        if (m.size() >= 4) {
            CapabilityData cap;
            cap.capabilityName = m[3].str();
            cap.interfaceClass = m[2].str();

            std::string implRegex = cap.capabilityName + "\\s*=\\s*new\\s+(\\w+)";
            std::regex implPattern(implRegex);
            std::smatch implMatch;
            if (std::regex_search(javaContent, implMatch, implPattern) && implMatch.size() > 1) {
                cap.implementationClass = implMatch[1].str();
            }

            cap.isAttachableToEntity = (javaContent.find("AttachCapabilitiesEvent<Entity>") != std::string::npos);
            cap.isAttachableToItem = (javaContent.find("AttachCapabilitiesEvent<ItemStack>") != std::string::npos);

            capabilities.push_back(cap);
        }
    }

    std::regex attachRegex(R"(AttachCapabilitiesEvent<(\w+)>\s+(\w+)\s*[={])");
    auto aBegin = std::sregex_iterator(javaContent.begin(), javaContent.end(), attachRegex);
    auto aEnd = std::sregex_iterator();

    for (auto it = aBegin; it != aEnd; ++it) {
        std::smatch m = *it;
        if (m.size() >= 3) {
            if (capabilities.empty()) {
                CapabilityData cap;
                cap.capabilityName = "unknown_capability";
                cap.interfaceClass = m[1].str();
                cap.isAttachableToEntity = (m[1].str() == "Entity");
                cap.isAttachableToItem = (m[1].str() == "ItemStack");
                capabilities.push_back(cap);
            }
        }
    }

    return capabilities;
}

std::vector<std::string> EventParser::ParseNetworkPackets(const std::string& javaContent) {
    std::vector<std::string> packets;

    std::regex packetRegex(R"(packet\s*\.registerMessage\s*\(\s*(\d+),\s*([\w.]+)\.class)");
    auto begin = std::sregex_iterator(javaContent.begin(), javaContent.end(), packetRegex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::smatch m = *it;
        if (m.size() >= 3) {
            packets.push_back(m[2].str());
        }
    }

    std::regex packetRegex2(R"(NetworkRegistry\.INSTANCE\.registerPacket\s*\(\s*([\w.]+)\.class)");
    auto begin2 = std::sregex_iterator(javaContent.begin(), javaContent.end(), packetRegex2);
    auto end2 = std::sregex_iterator();

    for (auto it = begin2; it != end2; ++it) {
        std::smatch m = *it;
        if (m.size() >= 2) {
            packets.push_back(m[1].str());
        }
    }

    return packets;
}