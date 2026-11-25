// ShapeDemo.cpp - LangBridge C++ -> Java example.
// Covers: pure-virtual interface, inheritance, templates, static constexpr,
//         overloads, std::vector, std::string methods, cout, try-catch.
#include <vector>
#include <string>
#include <iostream>

class Shape {
public:
    virtual double area() = 0;
};

class Circle : public Shape {
private:
    double radius;
public:
    Circle(double r) : radius(r) {}
    double area() override { return 3.14159 * radius * radius; }
    std::string describe() { return "circle r=" + std::to_string(radius); }
};

template <typename T>
class Box {
private:
    T item;
public:
    Box(T i) { item = i; }
    T get() { return item; }
    void set(T v) { item = v; }
};

class Counter {
private:
    int count = 0;
public:
    static constexpr int MAX = 10;
    void bump() { count++; }
    int value() { return count; }
    static int add(int a, int b) { return a + b; }
};

class Calc {
public:
    int compute(int a, int b) { return a + b; }
    double compute(double a, double b) { return a * b; }
};

class Ex {
public:
    void run() {
        Shape* s = new Circle(2.0);
        std::cout << "area=" << s->area() << std::endl;
        Box<std::string> b("x");
        b.set("hi");
        std::string got = b.get();
        if (got.size() > 0 && got.find('h') == 0) {
            std::cout << got.substr(0, 2) << std::endl;
        }
        std::vector<int> xs;
        for (int i = 0; i < 5; i++) { xs.push_back(i); }
        int sum = 0;
        for (int x : xs) { sum += x; }
        Calc c;
        int r = c.compute(1, 2);
        double d = c.compute(1.5, 2.5);
        std::cout << "sum=" << sum << " r=" << r << " d=" << d << std::endl;
        try {
            int q = std::stoi("42");
            std::cout << "q=" << q << std::endl;
        } catch (const std::invalid_argument& e) {
            std::cout << "bad" << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    Ex ex;
    ex.run();
    return 0;
}