// BlockModel.cpp
#include "BlockModel.h"
#include "util.h"
#include <set>
#include <functional>

namespace vcb {

void Block::RelinkReporters() {
    size_t idx = 0;
    for (auto& p : params) {
        if (p.type == ParamType::Reporter) {
            if (idx < reporters.size()) {
                p.reporter = &reporters[idx];
                p.hasReporter = true;
            } else {
                p.reporter = nullptr;
                p.hasReporter = false;
            }
            ++idx;
        }
    }
    for (auto& r : reporters) r.RelinkReporters();
    for (auto& c : children) c.RelinkReporters();
    for (auto& c : elseChildren) c.RelinkReporters();
}

JsonValue Block::ToJsonValue() const {
    JsonValue v = JsonValue::MakeObject();
    v.Set("id", JsonValue::MakeString(typeId));
    if (!category.empty()) v.Set("category", JsonValue::MakeString(category));
    if (!label.empty()) v.Set("label", JsonValue::MakeString(label));

    if (!params.empty()) {
        JsonValue arr = JsonValue::MakeArray();
        size_t repIdx = 0;
        for (const auto& p : params) {
            JsonValue pv = JsonValue::MakeObject();
            switch (p.type) {
                case ParamType::Text:    pv.Set("type", JsonValue::MakeString("text")); break;
                case ParamType::Number:  pv.Set("type", JsonValue::MakeString("num")); break;
                case ParamType::Boolean: pv.Set("type", JsonValue::MakeString("bool")); break;
                case ParamType::Reporter:
                    pv.Set("type", JsonValue::MakeString("reporter"));
                    if (repIdx < reporters.size()) {
                        pv.Set("block", reporters[repIdx].ToJsonValue());
                    }
                    ++repIdx;
                    break;
            }
            if (!p.text.empty()) pv.Set("value", JsonValue::MakeString(p.text));
            arr.Push(pv);
        }
        v.Set("params", arr);
    }
    if (!children.empty()) {
        JsonValue arr = JsonValue::MakeArray();
        for (const auto& c : children) arr.Push(c.ToJsonValue());
        v.Set("children", arr);
    }
    if (!elseChildren.empty()) {
        JsonValue arr = JsonValue::MakeArray();
        for (const auto& c : elseChildren) arr.Push(c.ToJsonValue());
        v.Set("else", arr);
    }
    return v;
}

std::string Block::ToJson() const {
    return DumpJson(ToJsonValue());
}

bool Block::FromJson(const JsonValue& v, Block& out, std::string& err) {
    out = Block();
    out.typeId = v.Get("id").AsString();
    out.category = v.Get("category").AsString();
    out.label = v.Get("label").AsString();

    if (v.Has("params") && v.Get("params").IsArray()) {
        for (const auto& pv : v.Get("params").arr) {
            std::string t = pv.Get("type").AsString("text");
            std::string val = pv.Get("value").AsString();
            if (t == "reporter") {
                Block rb;
                if (pv.Has("block")) {
                    if (!Block::FromJson(pv.Get("block"), rb, err)) return false;
                } else {
                    rb.typeId = "num";
                }
                out.AddReporter(std::move(rb));
            } else {
                Param p;
                if (t == "num") p.type = ParamType::Number;
                else if (t == "bool") p.type = ParamType::Boolean;
                else p.type = ParamType::Text;
                p.text = val;
                out.params.push_back(p);
            }
        }
    }
    if (v.Has("children") && v.Get("children").IsArray()) {
        for (const auto& cv : v.Get("children").arr) {
            Block cb;
            if (!Block::FromJson(cv, cb, err)) return false;
            out.children.push_back(std::move(cb));
        }
    }
    if (v.Has("else") && v.Get("else").IsArray()) {
        for (const auto& cv : v.Get("else").arr) {
            Block cb;
            if (!Block::FromJson(cv, cb, err)) return false;
            out.elseChildren.push_back(std::move(cb));
        }
    }
    out.RelinkReporters();
    return true;
}

std::string Program::ToJsonString() const {
    JsonValue root = JsonValue::MakeObject();
    root.Set("name", JsonValue::MakeString(name));
    JsonValue arr = JsonValue::MakeArray();
    for (const auto& b : blocks) arr.Push(b.ToJsonValue());
    root.Set("blocks", arr);
    return DumpJson(root);
}

bool Program::FromJsonString(const std::string& s, std::string& err) {
    JsonValue root;
    if (!ParseJson(s, root, err)) return false;
    if (!root.IsObject()) { err = "root is not object"; return false; }
    name = root.Get("name").AsString("Untitled");
    blocks.clear();
    if (root.Has("blocks") && root.Get("blocks").IsArray()) {
        for (const auto& bv : root.Get("blocks").arr) {
            Block b;
            if (!Block::FromJson(bv, b, err)) return false;
            blocks.push_back(std::move(b));
        }
    }
    return true;
}

std::vector<std::string> Program::CollectVariables() const {
    std::set<std::string> names;
    std::function<void(const Block&)> walk = [&](const Block& b) {
        // set/change 第一个参数是变量名
        if (b.typeId == "set" || b.typeId == "change") {
            if (!b.params.empty() && b.params[0].type == ParamType::Text) {
                names.insert(b.params[0].text);
            }
        }
        if (b.typeId == "var") {
            if (!b.params.empty() && b.params[0].type == ParamType::Text) {
                names.insert(b.params[0].text);
            }
        }
        for (const auto& r : b.reporters) walk(r);
        for (const auto& c : b.children) walk(c);
        for (const auto& c : b.elseChildren) walk(c);
    };
    for (const auto& b : blocks) walk(b);
    return std::vector<std::string>(names.begin(), names.end());
}

} // namespace vcb
