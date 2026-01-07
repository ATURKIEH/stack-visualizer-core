#include <stdio.h>

void foo(int x){
    int y = x + 1;
    printf("y = %d\n", y);
}

int main() {
    int a = 5;
    foo(a);
    return 0;
}