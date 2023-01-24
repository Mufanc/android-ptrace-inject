#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/unistd.h>

extern "C" void *dlopen(const char *, int);

extern "C" int dlclose(void *);

extern "C" void *__loader_dlopen(const char *, int, void *);

int main(int argc, char *argv[]) {
    printf("%d\n", getpid());
    fflush(stdout);

    if (argc >= 2) {
        fprintf(stderr, "dlopen!\n");
        void *handle = dlopen(argv[1], 1 /* RTLD_LAZY */);
        // if (dlclose(handle) == -1) {
        //    perror("dlclose");
        // }
    }

    for (int i = 0; i < 3; i++) {
        fprintf(stderr, "[ Target ] random: %ld\n", random() % 1000);
        sleep(1);
        if (argc == 1) i--;
    }
    
    FILE *maps = fopen("/proc/self/maps", "r");
    
    char buffer[256];
    ssize_t result;
    while ((result = fscanf(maps, "%*p-%*p %*s %*s %*s %*d %[^\n]", buffer))) {
        if (result == 0 || result == -1) break;
        if (!strstr(buffer, "inject")) continue;
        fprintf(stderr, "%s\n", buffer);
    }

    fclose(maps);

    return 0;
}

