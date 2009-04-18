#include <stdlib.h>
#include <stdio.h>
#include <time.h>

int main() {
    tzset();
    printf("timezone = %ld\n", timezone);
    return 0;
}
