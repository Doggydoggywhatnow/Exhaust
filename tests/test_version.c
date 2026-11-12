#include <stdio.h>
#include <string.h>
#include "exhaust.h"

int main(void) {
    if (strlen(exhaust_version()) == 0) {
        fprintf(stderr, "empty version\n");
        return 1;
    }
    return 0;
}
