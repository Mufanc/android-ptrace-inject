#include <cstdio>
#include <cstdlib>
#include <sys/unistd.h>
#include <android/log.h>

#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, "[  Hack  ]", __VA_ARGS__)

static int count = 0;

extern "C" int64_t random() {
    count++;
    if (count > 3) {
        fprintf(stderr, "Exiting...\n");
        exit(0);
    }
    return 0;
}

extern "C" [[gnu::constructor]] void __on_inject__() {
    if (access("/proc/self/fd/2", F_OK) == 0) {
        fprintf(stderr, "\x1b[31m[  Hack  ] Injected!\x1b[0m\n");
    } 
    ALOGI("[  Hack  ] Injected!");
}

