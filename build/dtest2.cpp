// Пробник 2: узнать настоящий UUID из libswdap
#include <dlfcn.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

struct effect_uuid_s { uint32_t timeLow; uint16_t timeMid; uint16_t timeHiAndVersion;
                       uint8_t clockSeq; uint8_t node[6]; };
typedef struct effect_uuid_s effect_uuid_t;
typedef struct effect_descriptor_s {
    effect_uuid_t type;
    effect_uuid_t uuid;
    uint32_t apiVersion;
    uint32_t flags;
    uint16_t cpuLoad;
    uint16_t memoryUsage;
    char name[64];
    char implementor[64];
} effect_descriptor_t;

static void printUuid(const char* label, const effect_uuid_t& u) {
    printf("  %s: %08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x\n", label,
           u.timeLow, u.timeMid, u.timeHiAndVersion, u.clockSeq,
           u.node[0], u.node[1], u.node[2], u.node[3], u.node[4], u.node[5]);
}

int main() {
    printf("== dtest2 ==\n");
    void* h = dlopen("/vendor/lib64/soundfx/libswdap.so", RTLD_NOW);
    if (!h) { printf("swdap FAIL: %s\n", dlerror()); return 1; }

    // 1) статический дескриптор dolby::DlbEffect::kDescriptor
    void* kd = dlsym(h, "_ZN5dolby9DlbEffect11kDescriptorE");
    printf("kDescriptor = %p\n", kd);
    if (kd) {
        const effect_descriptor_t* d = (const effect_descriptor_t*)kd;
        printUuid("kDescriptor.type", d->type);
        printUuid("kDescriptor.uuid", d->uuid);
        printf("  name='%s' implementor='%s'\n", d->name, d->implementor);
    }

    // 2) AELI->get_descriptor с нашим UUID и с UUID из kDescriptor
    struct aeli_s {
        uint32_t tag, version; const char* name; const char* impl;
        int32_t (*create_effect)(const effect_uuid_t*, int32_t, int32_t, void**);
        int32_t (*release_effect)(void*);
        int32_t (*get_descriptor)(const effect_uuid_t*, effect_descriptor_t*);
    };
    auto* a = (aeli_s*)dlsym(h, "AELI");
    if (!a) { printf("AELI not found\n"); return 1; }
    printf("AELI tag=0x%x version=0x%x name=%s\n", a->tag, a->version, a->name);

    effect_uuid_t ours{};
    ours.timeLow = 0x9d4921da; ours.timeMid = 0x8225; ours.timeHiAndVersion = 0x4f29;
    ours.clockSeq = 0xae;
    ours.node[0]=0xfa; ours.node[1]=0x39; ours.node[2]=0x53;
    ours.node[3]=0x7a; ours.node[4]=0x04; ours.node[5]=0xbc;
    effect_descriptor_t od{};
    int32_t r = a->get_descriptor(&ours, &od);
    printf("get_descriptor(9d4921da) -> %d\n", r);
    if (r == 0) { printUuid(" answer.uuid", od.uuid); printf("  name='%s'\n", od.name); }

    if (kd) {
        const effect_descriptor_t* d = (const effect_descriptor_t*)kd;
        effect_descriptor_t od2{};
        int32_t r2 = a->get_descriptor(&d->uuid, &od2);
        printf("get_descriptor(kDescriptor.uuid) -> %d\n", r2);
        if (r2 == 0) { printUuid(" answer.uuid", od2.uuid); printf("  name='%s'\n", od2.name); }
        // попробуем создать
        void* handle = nullptr;
        int32_t r3 = a->create_effect(&d->uuid, 0, 0, &handle);
        printf("create_effect(kDescriptor.uuid,0,0) -> %d handle=%p\n", r3, handle);
        if (r3 == 0 && handle) a->release_effect(handle);
    }
    return 0;
}
