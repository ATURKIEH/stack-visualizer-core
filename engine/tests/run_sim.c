#include <stdio.h>
#include "sv_engine.h"

int main(void) {
    const char* json = simulate("dummy input", 0);
    printf("%s\n", json);
    return 0;
}
