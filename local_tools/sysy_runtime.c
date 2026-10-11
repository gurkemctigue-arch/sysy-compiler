#include <stdio.h>
#include <time.h>

int getint(void) {
    int x = 0;
    if (scanf("%d", &x) != 1) {
        return 0;
    }
    return x;
}

int getch(void) {
    int c = getchar();
    return c == EOF ? -1 : c;
}

int getarray(int a[]) {
    int n = 0;
    if (scanf("%d", &n) != 1) {
        return 0;
    }
    for (int i = 0; i < n; ++i) {
        scanf("%d", &a[i]);
    }
    return n;
}

void putint(int x) {
    printf("%d", x);
}

void putch(int x) {
    putchar(x);
}

void putarray(int n, int a[]) {
    printf("%d:", n);
    for (int i = 0; i < n; ++i) {
        printf(" %d", a[i]);
    }
    putchar('\n');
}

float getfloat(void) {
    float x = 0.0f;
    if (scanf("%f", &x) != 1) {
        return 0.0f;
    }
    return x;
}

int getfarray(float a[]) {
    int n = 0;
    if (scanf("%d", &n) != 1) {
        return 0;
    }
    for (int i = 0; i < n; ++i) {
        scanf("%f", &a[i]);
    }
    return n;
}

void putfloat(float x) {
    printf("%a", (double)x);
}

void putfarray(int n, float a[]) {
    printf("%d:", n);
    for (int i = 0; i < n; ++i) {
        printf(" %a", (double)a[i]);
    }
    putchar('\n');
}

void starttime(void) {}
void stoptime(void) {}
void _sysy_starttime(int line) { (void)line; }
void _sysy_stoptime(int line) { (void)line; }
