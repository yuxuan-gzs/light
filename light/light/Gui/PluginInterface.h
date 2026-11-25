#pragma once
// 兼容旧路径：所有 GUI/普通插件必须引用 light 主程序的同一份插件 ABI。
// 若在 Gui 目录重新定义 IPlugin/IGuiProvider，会导致重复定义和类型不可转换。
#include "../PluginInterface.h"
