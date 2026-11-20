// ShapeDemo.java - LangBridge Java -> C++ example.
// Covers: interface, inheritance, generics, static members, overloads,
//         List/Map containers, enhanced for, String methods, switch, try-catch.
import java.util.*;

interface Shape {
    double area();
}

class Circle implements Shape {
    private double radius;
    public Circle(double r) { radius = r; }
    public double area() { return 3.14159 * radius * radius; }
    public String describe() { return "circle r=" + radius; }
}

class Box<T> {
    private T item;
    public Box(T i) { item = i; }
    public T get() { return item; }
    public void set(T v) { item = v; }
}

class Counter {
    private int count = 0;
    public static final int MAX = 10;
    public void bump() { count++; }
    public int value() { return count; }
    public static int add(int a, int b) { return a + b; }
}

class Calc {
    public int compute(int a, int b) { return a + b; }
    public double compute(double a, double b) { return a * b; }
    public String merge(String a, String b) {
        return a + "-" + b;
    }
}

class Ex {
    public void run() {
        Shape s = new Circle(2.0);
        System.out.println("area=" + s.area());
        Box<String> b = new Box<String>("hi");
        String got = b.get();
        if (got.length() > 0 && got.startsWith("h")) {
            System.out.println(got.toUpperCase());
        }
        List<Integer> xs = new ArrayList<Integer>();
        for (int i = 0; i < 5; i++) { xs.add(i); }
        int sum = 0;
        for (int x : xs) { sum += x; }
        Map<String, Integer> m = new HashMap<String, Integer>();
        m.put("a", 1);
        m.put("b", 2);
        for (String k : m.keySet()) { sum += m.get(k); }
        Calc c = new Calc();
        int r = c.compute(1, 2);
        double d = c.compute(1.5, 2.5);
        System.out.println("sum=" + sum + " r=" + r + " d=" + d);
        try {
            int q = Integer.parseInt("42");
            System.out.println("q=" + q);
        } catch (NumberFormatException e) {
            System.out.println("bad");
        }
        switch (got) {
            case "hi": System.out.println("switch-hi"); break;
            default: System.out.println("switch-other"); break;
        }
        Counter.add(MAX, 1);
    }
}

class ProbeMain {
    public static void main(String[] args) {
        Ex ex = new Ex();
        ex.run();
    }
}