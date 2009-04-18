#include <time.h>
#include <stdio.h>

int main() {
    tzset();
    time_t t = time(NULL);
    printf("Timezone name is %s, timezone=%ld\n", __tzname[1], timezone);
    printf("The time is %s", ctime(&t));
    printf("Timezone name is %s, timezone=%ld\n", __tzname[1], timezone);
    return 0;
}
