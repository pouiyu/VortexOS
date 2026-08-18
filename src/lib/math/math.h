#ifndef _MATH_H
#define _MATH_H

#include <sys/cdefs.h>

#define M_PI 3.14159265358979323846
#define M_E  2.71828182845904523536

__BEGIN_DECLS

double sin(double x);
double cos(double x);
double tan(double x);

double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);

double sinh(double x);
double cosh(double x);
double tanh(double x);

double exp(double x);
double log(double x);
double log10(double x);
double log2(double x);

double pow(double base, double exp);
double sqrt(double x);
double cbrt(double x);

double fabs(double x);
double floor(double x);
double ceil(double x);
double round(double x);
double trunc(double x);

double fmod(double x, double y);

__END_DECLS

#endif