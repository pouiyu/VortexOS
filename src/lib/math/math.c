#include "math.h"

// 泰勒级数实现 sin
double sin(double x) {
    // 规范化到 [-π, π]
    while (x > M_PI) x -= 2 * M_PI;
    while (x < -M_PI) x += 2 * M_PI;

    double result = 0;
    double term = x;
    double x2 = x * x;
    int n = 1;

    // sin(x) = x - x³/3! + x⁵/5! - x⁷/7! + ...
    for (int i = 0; i < 10; i++) {
        result += term;
        term *= -x2 / ((n + 1) * (n + 2));
        n += 2;
    }
    return result;
}

double cos(double x) {
    return sin(x + M_PI / 2);
}

double tan(double x) {
    return sin(x) / cos(x);
}

double fabs(double x) {
    return x < 0 ? -x : x;
}

// 牛顿法实现 sqrt
double sqrt(double x) {
    if (x <= 0) return 0;

    double guess = x;
    for (int i = 0; i < 50; i++) {
        guess = (guess + x / guess) / 2;
    }
    return guess;
}

double pow(double base, double exp) {
    if (exp == 0) return 1;
    if (exp < 0) return 1 / pow(base, -exp);
    if (exp == (int)exp) {
        double result = 1;
        for (int i = 0; i < (int)exp; i++) {
            result *= base;
        }
        return result;
    }
    return exp * log(base);
}

double exp(double x) {
    double result = 1;
    double term = 1;
    for (int i = 1; i < 30; i++) {
        term *= x / i;
        result += term;
    }
    return result;
}

double log(double x) {
    if (x <= 0) return 0;
    if (x < 1) return -log(1 / x);

    double result = 0;
    double y = (x - 1) / (x + 1);
    double y2 = y * y;
    double term = y;

    // ln(x) = 2 * (y + y³/3 + y⁵/5 + ...)
    for (int i = 1; i < 50; i += 2) {
        result += term / i;
        term *= y2;
    }
    return 2 * result;
}

double log10(double x) {
    return log(x) / log(10);
}

double log2(double x) {
    return log(x) / log(2);
}

double atan(double x) {
    if (x < -1) return -M_PI / 2 - atan(1 / x);
    if (x > 1) return M_PI / 2 - atan(1 / x);

    double result = 0;
    double term = x;
    double x2 = x * x;
    int sign = 1;

    // atan(x) = x - x³/3 + x⁵/5 - ...
    for (int i = 1; i < 30; i += 2) {
        result += sign * term / i;
        term *= x2;
        sign = -sign;
    }
    return result;
}

double atan2(double y, double x) {
    if (x > 0) return atan(y / x);
    if (x < 0 && y >= 0) return atan(y / x) + M_PI;
    if (x < 0 && y < 0) return atan(y / x) - M_PI;
    if (x == 0 && y > 0) return M_PI / 2;
    if (x == 0 && y < 0) return -M_PI / 2;
    return 0;
}

double asin(double x) {
    if (x < -1 || x > 1) return 0;
    return atan(x / sqrt(1 - x * x));
}

double acos(double x) {
    if (x < -1 || x > 1) return 0;
    return M_PI / 2 - asin(x);
}

double sinh(double x) {
    return (exp(x) - exp(-x)) / 2;
}

double cosh(double x) {
    return (exp(x) + exp(-x)) / 2;
}

double tanh(double x) {
    return sinh(x) / cosh(x);
}

double floor(double x) {
    return (double)(int)x;
}

double ceil(double x) {
    int i = (int)x;
    return (x > i) ? (double)(i + 1) : (double)i;
}

double round(double x) {
    return (x >= 0) ? floor(x + 0.5) : ceil(x - 0.5);
}

double trunc(double x) {
    return (double)(int)x;
}

double fmod(double x, double y) {
    if (y == 0) return 0;
    return x - (int)(x / y) * y;
}

double cbrt(double x) {
    if (x == 0) return 0;
    return (x > 0) ? pow(x, 1.0/3.0) : -pow(-x, 1.0/3.0);
}