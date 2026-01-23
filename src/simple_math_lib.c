//trunc() implementation in Java:
#include <stdlib.h>
// --- Standard Library Functions ---
double simple_sqrt(double val) {
    double x = val;
    double y = (x + 1) / 2;
    while (abs(y - x) > 0.0000001) {
        x = y;
        y = (x + val / x) / 2.;
    }
    return y;
}

double simple_sin(double x) {
    // Basic range reduction to [-pi, pi]
    while (x > 3.1415926535) x -= 6.283185307;
    while (x < -3.1415926535) x += 6.283185307;

    double res = 0, term = x;
    double x2 = x * x;
    int i = 1;
    for (int n = 1; n < 10; n++) { // 10 iterations for high precision
        res += term;
        term *= -x2 / ((2 * n) * (2 * n + 1));
    }
    return res;
}

double simple_cos(double x) {
    return simple_sin(x + (3.14159265358 / 2.));
}
int simple_floor(double v)
{
    int t;
    if(v<0){
        t=(int)v +(-1);
        return t;
    }
    return v;
}
int simple_ceil(double v)
{
    if(v-(int)v==0)    // check weather the float variable contains a integer value or not //
        return v;
    return simple_floor(v)+1;
}
double simple_truncate(double x) {
    return x < 0 ? -simple_floor(-x) : simple_floor(x);
}

double simple_fmod(double x, double y) {
    return x - simple_truncate(x / y) * y;
}