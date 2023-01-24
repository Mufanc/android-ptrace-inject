#include <cstdio>
#include <cstdlib>

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
    fprintf(stderr, "Injected!\n");
}

