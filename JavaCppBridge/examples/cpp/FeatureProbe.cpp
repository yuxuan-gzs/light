// FeatureProbe.cpp - LangBridge C++ -> Java feature probe.
//
// Exercises the parser / generator paths that large real-world sources hit:
// object and function macros, #if/#else/#endif and defined() conditionals,
// wide and adjacent string literals, range-for, out-of-line member
// definitions, file-scope globals, namespaces, enums, shift operators,
// catch-all handlers and atomic flags.
//
// Not covered here: lambdas bound to a variable ('auto f = [](int x){...};').
// Java cannot infer 'var' for a lambda and has no structural function type,
// so that pattern stays a documented limitation (see README).
//
// This file is valid C++17 on its own: `cl /std:c++17` / `g++ -std=c++17`
// compile it, and the output below is also what the generated Java prints.
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <stdexcept>

#define VERSION 3
#define SQUARE(x) ((x) * (x))
#define TAG "probe"

#if VERSION >= 3
#define TIER "pro"
#else
#define TIER "basic"
#endif

#ifdef TAG
#define HAS_TAG 1
#else
#define HAS_TAG 0
#endif

namespace probe {

// No explicit enumerator values: Java enum ordinals then match the C++ ones.
enum Mode { MODE_FAST, MODE_SLOW };

std::atomic<bool> g_ready(false);
int g_calls = 0;
std::string g_label = std::string("label-") + TIER;

// ---- out-of-line member definitions -------------------------------------
class Accumulator {
private:
    int total;
public:
    Accumulator() : total(0) {}
    void add(int v);
    int value() const;
    void reset();
};

void Accumulator::add(int v) { total += v; g_calls++; }
int Accumulator::value() const { return total; }
void Accumulator::reset() { total = 0; }

// ---- literals ------------------------------------------------------------
std::string joined() { return "feature" "-" "probe"; }
std::wstring wide() { return L"wide" L"-ok"; }

// ---- operators -----------------------------------------------------------
int shifty(int v) { return (v << 2) >> 1; }

// ---- recursion / accumulation --------------------------------------------
int applyTwice(int v) { return v + v; }

int rangeSum(const std::vector<int>& xs) {
    int total = 0;
    for (int x : xs) { total += x; }
    return total;
}

// ---- catch-all -----------------------------------------------------------
int guarded(int v) {
    try {
        if (v < 0) throw std::runtime_error("negative");
        return v;
    } catch (const std::exception& e) {
        return -1;
    } catch (...) {
        return -2;
    }
}

class Runner {
private:
    Mode mode;
public:
    Runner() : mode(MODE_FAST) {}
    int modeValue() const { return (int)mode; }

    int run() {
        Accumulator acc;
        acc.add(SQUARE(3));
        acc.add(1);

        std::vector<int> xs;
        xs.push_back(1);
        xs.push_back(2);
        xs.push_back(3);

        std::map<std::string, int> m;
        m["a"] = 1;
        m["b"] = 2;

        g_ready = true;

        std::cout << "tag=" << TAG << " tier=" << TIER << " hasTag=" << HAS_TAG << std::endl;
        std::cout << "square=" << SQUARE(4) << " shift=" << shifty(10) << std::endl;
        std::cout << "sum=" << rangeSum(xs) << " acc=" << acc.value()
                  << " calls=" << g_calls << std::endl;
        std::cout << "joined=" << joined() << " twice=" << applyTwice(21) << std::endl;
        std::cout << "mapA=" << m["a"] << " mapSize=" << m.size()
                  << " guarded=" << guarded(-5) << std::endl;
        std::cout << "ready=" << (g_ready ? 1 : 0) << " label=" << g_label
                  << " mode=" << modeValue() << std::endl;

        acc.reset();
        return acc.value();
    }
};

}  // namespace probe

int main(int argc, char* argv[]) {
    probe::Runner r;
    int left = r.run();
    std::cout << "reset=" << left << std::endl;
    return 0;
}
