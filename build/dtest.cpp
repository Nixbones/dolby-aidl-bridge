#include <dlfcn.h>
#include <cstdio>
static void probe(void* h, const char* name) { printf("  %s -> %p\n", name, dlsym(h, name)); }
int main() {
    printf("== dtest ==\n");
    void* h = dlopen("libdolbyaidlshim.so", RTLD_NOW);
    if (!h) { printf("shim FAIL: %s\n", dlerror()); }
    else {
        printf("shim OK (%p)\n", h);
        probe(h, "createEffect"); probe(h, "queryEffect"); probe(h, "destroyEffect");
    }
    void* d = dlopen("libswdap.so", RTLD_NOW);
    if (!d) printf("swdap FAIL: %s\n", dlerror());
    else printf("swdap OK (%p), AELI -> %p\n", d, dlsym(d, "AELI"));
    void* s2 = dlopen("/vendor/lib64/soundfx/libswdap.so", RTLD_NOW);
    if (!s2) printf("swdap(path) FAIL: %s\n", dlerror());
    else printf("swdap(path) OK, AELI -> %p\n", dlsym(s2, "AELI"));
    return 0;
}
