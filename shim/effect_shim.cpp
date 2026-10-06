// =============================================================================
// libdolbyaidlshim.so - мост AELI (legacy Dolby DAP) -> AIDL audio effect HAL V2
//
// Реализует контракты, которые ждёт AIDL EffectFactory (dlsym):
//   extern "C" binder_exception_t createEffect(const AudioUuid*, std::shared_ptr<IEffect>*)
//   extern "C" binder_exception_t queryEffect (const AudioUuid*, Descriptor*)
//   extern "C" binder_exception_t destroyEffect(const std::shared_ptr<IEffect>&)
//
// Внутри dlopen'ит libswdap.so, берёт у неё структуру AELI
// (audio_effect_library_t, API 3.1) и проксирует вызовы.
// =============================================================================
#define LOG_TAG "DolbyAidlShim"
#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <android/binder_ibinder.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <string>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <vector>

#include <aidl/android/hardware/audio/effect/BnEffect.h>
#include <aidl/android/hardware/audio/effect/Descriptor.h>
#include <aidl/android/hardware/audio/effect/IEffect.h>
#include <aidl/android/hardware/audio/effect/Parameter.h>
#include <aidl/android/hardware/audio/effect/State.h>
#include <aidl/android/hardware/audio/effect/CommandId.h>
#include <aidl/android/media/audio/common/AudioUuid.h>
#include <fmq/AidlMessageQueue.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <climits>
#include <fcntl.h>
#include <cstdarg>
#include <cstdio>

using aidl::android::hardware::audio::effect::BnEffect;
using aidl::android::hardware::audio::effect::CommandId;
using aidl::android::hardware::audio::effect::Descriptor;
using aidl::android::hardware::audio::effect::IEffect;
using aidl::android::hardware::audio::effect::Parameter;
using aidl::android::hardware::audio::effect::State;
using aidl::android::media::audio::common::AudioUuid;
using ::android::AidlMessageQueue;
namespace fmq = ::aidl::android::hardware::common::fmq;

// файловый лог: logcat у HAL-процесса режется квотой, файл надёжнее
static void shimLog(const char* fmt, ...) {
    int fd = open("/data/local/tmp/dolby_shim.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0 && n < static_cast<int>(sizeof(buf)) - 1) {
        buf[n] = 0x0A;  // перевод строки
        write(fd, buf, static_cast<size_t>(n) + 1);
    }
    close(fd);
}

#define ALOGI(...) do { __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); shimLog(__VA_ARGS__); } while (0)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// официальные значения из system/media audio_effects/aidl_effects_utils.h
static constexpr uint32_t kEventFlagNotEmpty       = 0x1;        // deprecated в V2
static constexpr uint32_t kEventFlagDataMqUpdate   = 0x1 << 10;
static constexpr uint32_t kEventFlagDataMqNotEmpty = 0x1 << 11;
static constexpr int32_t  kReopenSupportedVersion  = 2;
// служебные биты FMQ (MessageQueueBase.h): клиент ждёт статус на FMQ_NOT_EMPTY
static constexpr uint32_t kFmqNotFull  = 0x1;
static constexpr uint32_t kFmqNotEmpty = 0x2;

// ===== локальные реализации мелочей из libfmq (иначе тянется std::__1 ABI) =====
namespace android {
namespace hardware {
namespace details {
void logError(const std::string& msg) {
    __android_log_print(ANDROID_LOG_ERROR, "FMQ", "%s", msg.c_str());
}
void check(bool cond, const char* msg) {
    if (!cond) __android_log_print(ANDROID_LOG_ERROR, "FMQ", "check failed: %s", msg);
}
void errorWriteLog(int fd, const char* msg) {
    __android_log_print(ANDROID_LOG_ERROR, "FMQ", "fd %d: %s", fd, msg);
}
}  // namespace details
}  // namespace hardware
}  // namespace android

// ===== минимальный EventFlag через futex (совместимо с libfmq-протоколом) =====
// (копия логики android::hardware::EventFlag из system/libfmq/EventFlag.cpp,
//  чтобы не зависеть от ABI системного libc++: символы той lib mangled как std::__1)
static int ef_wake(uint32_t* word, uint32_t bitmask) {
    if (!word || !bitmask) return -EINVAL;
    uint32_t old = __atomic_fetch_or(word, bitmask, __ATOMIC_SEQ_CST);
    if ((~old & bitmask) != 0) {
        long r = syscall(__NR_futex, word, FUTEX_WAKE_BITSET, INT_MAX, nullptr, nullptr, bitmask);
        if (r == -1) return -errno;
    }
    return 0;
}

static int ef_wait(uint32_t* word, uint32_t bitmask, uint64_t timeoutNs, uint32_t* outState) {
    if (!word || !bitmask || !outState) return -EINVAL;
    uint32_t old = __atomic_fetch_and(word, ~bitmask, __ATOMIC_SEQ_CST);
    uint32_t setBits = old & bitmask;
    if (setBits != 0) { *outState = setBits; return 0; }
    uint32_t expected = old & ~bitmask;
    int ret;
    if (timeoutNs) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t total = static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
                         static_cast<uint64_t>(ts.tv_nsec) + timeoutNs;
        ts.tv_sec = static_cast<time_t>(total / 1000000000ULL);
        ts.tv_nsec = static_cast<long>(total % 1000000000ULL);
        ret = syscall(__NR_futex, word, FUTEX_WAIT_BITSET, expected, &ts, nullptr, bitmask);
    } else {
        ret = syscall(__NR_futex, word, FUTEX_WAIT_BITSET, expected, nullptr, nullptr, bitmask);
    }
    if (ret == -1) { *outState = 0; return -errno; }
    old = __atomic_fetch_and(word, ~bitmask, __ATOMIC_SEQ_CST);
    *outState = old & bitmask;
    return (*outState) ? 0 : -EINTR;
}

// ============================ legacy типы (audio_effect.h) ===================
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
} effect_descriptor_t_2;
// ВАЖНО: раскладка Dolby (дизасм AudioBufferProvider::set в libswdap):
//   +0: frameCount (uint32), +8: указатель на данные. У AOSP наоборот!
struct audio_buffer_s {
    uint32_t frameCount;
    uint32_t _pad;
    union { void* raw; int32_t* s32; int16_t* s16; uint8_t* u8; float* f32; } audio;
};
typedef struct audio_buffer_s audio_buffer_t;
struct effect_interface_s;
typedef struct effect_interface_s** effect_handle_t;

struct audio_effect_library_s {
    uint32_t tag;
    uint32_t version;
    const char* name;
    const char* implementor;
    int32_t (*create_effect)(const effect_uuid_t*, int32_t sessionId, int32_t ioId,
                             effect_handle_t* pHandle);
    int32_t (*release_effect)(effect_handle_t);
    int32_t (*get_descriptor)(const effect_uuid_t*, effect_descriptor_t_2*);
};

struct effect_interface_s {
    int32_t (*process)(effect_handle_t self, audio_buffer_t* in, audio_buffer_t* out);
    int32_t (*command)(effect_handle_t self, uint32_t cmdCode, uint32_t cmdSize, void* pCmdData,
                       uint32_t* replySize, void* pReplyData);
    int32_t (*get_descriptor)(effect_handle_t self, effect_descriptor_t_2* pDescriptor);
    int32_t (*process_reverse)(effect_handle_t self, audio_buffer_t* in, audio_buffer_t* out);
};

// точные значения из hardware/libhardware include_all/hardware/audio_effect.h (enum order!)
// INIT=0 SET_CONFIG=1 RESET=2 ENABLE=3 DISABLE=4 SET_PARAM=5 DEFERRED=6 COMMIT=7
// GET_PARAM=8 SET_DEVICE=9 SET_VOLUME=10 SET_AUDIO_MODE=11 ... GET_CONFIG=14
static const uint32_t CMD_INIT = 0;             // EFFECT_CMD_INIT (обязательна!)
static const uint32_t CMD_SET_CONFIG = 1;
static const uint32_t CMD_RESET = 2;
static const uint32_t CMD_ENABLE = 3;
static const uint32_t CMD_DISABLE = 4;
static const uint32_t CMD_SET_DEVICE = 9;
static const uint32_t CMD_SET_VOLUME = 10;
static const uint32_t CMD_SET_AUDIO_MODE = 11;
static const uint32_t CMD_GET_CONFIG = 14;
static const uint32_t CMD_SET_PARAM = 5;        // EFFECT_CMD_SET_PARAM

// ===== DAP (Dolby Audio Processing) ==========================================
// Формат параметра (подтверждён дизасмом DapEffectContext::setParamValues /
// EffectParamParser): effect_param_t {status, psize=4, vsize, paramId=0(SET_VALUES)}
// + блоб [activeDevice u32][numParams u32] { [deviceId u32][paramId u32]
//   [numValues u32][values i32 *n] }...
// Адресация параметров - 4CC-код, записанный как 4 байта ASCII (LE u32).
static constexpr uint32_t P4(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}
static constexpr uint32_t P3(char a, char b, char c) { return P4(a, b, c, '\0'); }

struct DapParam {
    uint32_t id;
    uint32_t n;
    int32_t v[8];
};
static const size_t kMaxDapParams = 48;
using DapParamList = std::vector<DapParam>;

static const uint32_t AUDIO_EFFECT_LIBRARY_TAG_L = (('A' << 24) | ('E' << 16) | ('L' << 8) | ('T'));
static const uint32_t EFFECT_CONFIG_ALL = 31;  // samples|channels|format|accMode|provider
static const uint32_t AUDIO_CHANNEL_OUT_STEREO_L = 0xCu;   // AUDIO_CHANNEL_OUT_STEREO
static const uint32_t AUDIO_CHANNEL_INDEX_STEREO = 0x3u;   // index-маска L|R
static const uint32_t AUDIO_DEVICE_OUT_SPEAKER_L = 0x2u;
static const int32_t VOLUME_U24_8_ONE = 0x1000000;
static const uint8_t LEGACY_FORMAT_FLOAT = 5;  // AUDIO_FORMAT_PCM_FLOAT (SUB_FLOAT=0x5!)
static const uint8_t EFFECT_BUFFER_ACCESS_READ = 0;
static const uint8_t EFFECT_BUFFER_ACCESS_WRITE = 1;

// audio_buffer_provider_t: ДВА коллбэка + cookie (24 байта на arm64!)
typedef int32_t (*buffer_callback_t)(void* cookie, audio_buffer_t* buffer);
typedef struct audio_buffer_provider_s {
    buffer_callback_t getNextBuffer;
    buffer_callback_t releaseBuffer;
    void* cookie;
} audio_buffer_provider_t;

typedef struct buffer_config_s {
    audio_buffer_t buffer;
    uint32_t samplingRate;
    uint32_t channels;
    audio_buffer_provider_t bufferProvider;
    uint8_t format;         // PCM sub-format: 4 = float
    uint8_t accessMode;
    uint16_t mask;
} buffer_config_t;
typedef struct effect_config_s { buffer_config_t inputCfg; buffer_config_t outputCfg; }
    effect_config_t;

// ============================ Dolby UUID ====================================
static const AudioUuid kDapUuid = { static_cast<int32_t>(0x9d4921dau), 0x8225, 0x4f29,
                                    static_cast<int32_t>(0xaefau),
                                    { 0x39, 0x53, 0x7a, 0x04, 0xbc, 0xaa } };
static const AudioUuid kDapTypeUuid = { static_cast<int32_t>(0xe119e520u), 0x7dfc, 0x4a15,
                                        static_cast<int32_t>(0xb1fau),
                                        { 0x23, 0x4a, 0x09, 0x39, 0xf0, 0x82 } };

// ============================ AELI loader ===================================
namespace {
struct Aeli {
    void* lib = nullptr;
    audio_effect_library_s* desc = nullptr;
    effect_descriptor_t_2 legacyDesc{};   // настоящий дескриптор от Dolby
    bool haveDesc = false;
    bool ok = false;

    bool load();
    const effect_uuid_t* realUuid() const {
        return haveDesc ? &legacyDesc.uuid : nullptr;
    }
};

Aeli& aeli() { static Aeli a; return a; }

bool Aeli::load() {
    if (ok) return true;
    const char* candidates[] = {
        "libswdap.so",
        "/vendor/lib64/soundfx/libswdap.so",
        "/odm/lib64/soundfx/libswdap.so",
        "/vendor/lib64/libswdap.so",
        "libswdap_dlb.so",
        nullptr };
    for (int i = 0; candidates[i]; i++) {
        void* h = dlopen(candidates[i], RTLD_NOW);
        ALOGI("dlopen(%s) = %p %s", candidates[i], h, h ? "" : dlerror());
        if (!h) continue;
        void* sym = dlsym(h, "AELI");
        if (!sym) { dlclose(h); continue; }
        auto* d = static_cast<audio_effect_library_s*>(sym);

        // настоящий uuid берём прямо из libswdap (dolby::DlbEffect::kDescriptor)
        if (void* kd = dlsym(h, "_ZN5dolby9DlbEffect11kDescriptorE")) {
            legacyDesc = *static_cast<effect_descriptor_t_2*>(kd);
            haveDesc = true;
            char buf[64];
            snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                     legacyDesc.uuid.timeLow, legacyDesc.uuid.timeMid,
                     legacyDesc.uuid.timeHiAndVersion, legacyDesc.uuid.clockSeq,
                     legacyDesc.name[0] ? 0 : 0,
                     0, 0, 0, 0, 0, 0);
            ALOGI("Dolby kDescriptor: name='%s' impl='%s'", legacyDesc.name, legacyDesc.implementor);
            ALOGI("Dolby real uuid: timeLow=%08x timeMid=%04x timeHi=%04x clockSeq=%02x node=%02x%02x%02x%02x%02x%02x",
                  legacyDesc.uuid.timeLow, legacyDesc.uuid.timeMid, legacyDesc.uuid.timeHiAndVersion,
                  legacyDesc.uuid.clockSeq, legacyDesc.uuid.node[0], legacyDesc.uuid.node[1],
                  legacyDesc.uuid.node[2], legacyDesc.uuid.node[3], legacyDesc.uuid.node[4],
                  legacyDesc.uuid.node[5]);
        }
        ALOGI("AELI: tag=0x%x version=0x%x name=%s impl=%s", d->tag, d->version,
              d->name ? d->name : "?", d->implementor ? d->implementor : "?");
        if (d->tag == AUDIO_EFFECT_LIBRARY_TAG_L && d->create_effect && d->release_effect &&
            d->get_descriptor) {
            lib = h; desc = d; ok = true;
            return true;
        }
        dlclose(h);
    }
    return false;
}

AudioUuid toAidlUuid(const effect_uuid_t& u) {
    AudioUuid a;
    a.timeLow = static_cast<int32_t>(u.timeLow);
    a.timeMid = static_cast<int32_t>(u.timeMid);
    a.timeHiAndVersion = static_cast<int32_t>(u.timeHiAndVersion);
    a.clockSeq = static_cast<int32_t>(u.clockSeq);
    a.node = std::vector<uint8_t>(u.node, u.node + 6);
    return a;
}
effect_uuid_t toLegacyUuid(const AudioUuid& a) {
    effect_uuid_t u{};
    u.timeLow = static_cast<uint32_t>(a.timeLow);
    u.timeMid = static_cast<uint16_t>(a.timeMid);
    u.timeHiAndVersion = static_cast<uint16_t>(a.timeHiAndVersion);
    u.clockSeq = static_cast<uint8_t>(a.clockSeq);
    for (int i = 0; i < 6; i++) u.node[i] = a.node[i];
    return u;
}

bool uuidEquals(const AudioUuid& a, const AudioUuid& b) {
    if (a.timeLow != b.timeLow || a.timeMid != b.timeMid ||
        a.timeHiAndVersion != b.timeHiAndVersion || a.clockSeq != b.clockSeq ||
        a.node.size() != 6)
        return false;
    for (int i = 0; i < 6; i++)
        if (a.node[i] != b.node[i]) return false;
    return true;
}
}  // namespace

// ============================ эффект-объект =================================
namespace {

using StatusMQ = AidlMessageQueue<IEffect::Status, fmq::SynchronizedReadWrite>;
using DataMQ = AidlMessageQueue<float, fmq::SynchronizedReadWrite>;

class DapEffect : public BnEffect {
  public:
    explicit DapEffect(const Descriptor& desc) : mAidlDesc(desc) {}
    ~DapEffect() override { closeLegacy(); }

    // ---- IEffect V2 ----
    ::ndk::ScopedAStatus open(const Parameter::Common& common,
                              const std::optional<Parameter::Specific>& specific,
                              IEffect::OpenEffectReturn* ret) override;
    ::ndk::ScopedAStatus close() override;
    ::ndk::ScopedAStatus getDescriptor(Descriptor* _aidl_return) override;
    ::ndk::ScopedAStatus command(CommandId in_commandId) override;
    ::ndk::ScopedAStatus getState(State* _aidl_return) override;
    ::ndk::ScopedAStatus setParameter(const Parameter& in_param) override;
    ::ndk::ScopedAStatus getParameter(const Parameter::Id& in_paramId,
                                      Parameter* _aidl_return) override;
    ::ndk::ScopedAStatus reopen(IEffect::OpenEffectReturn* ret) override;
  private:
    Descriptor mAidlDesc;
    std::mutex mMutex;
    State mState = State::INIT;

    effect_handle_t mHandle = nullptr;
    int32_t mSession = 0;
    uint32_t mSamplingRate = 48000;
    uint32_t mChannels = AUDIO_CHANNEL_OUT_STEREO_L;
    uint32_t mFrameCount = 0;
    bool mLegacyEnabled = false;
    std::vector<int32_t> mVolume = { 1000, 1000 };

    std::unique_ptr<StatusMQ> mStatusMQ;
    std::unique_ptr<DataMQ> mInputMQ, mOutputMQ;
    uint32_t* mEvFlagWord = nullptr;
    std::thread mWorker;
    std::atomic<bool> mRun{false};

    bool createLegacy(int32_t session, int32_t ioHandle);
    void closeLegacy();
    bool pushConfig();
    void workerLoop();
    size_t channelCount() const;

    // DAP-параметры
    bool sendSetValues(const DapParamList& ps);
    bool applyAtmos(bool on);
    std::atomic<int> mApplyPending{-1};   // -1=нет, 0=выключить, 1=включить
    int mLoopsSinceCheck = 0;
    uint64_t mParamsFileSig = 0;
    bool mAmosOn = false;
    bool mHeadphone = false;
    uint32_t mDevice = AUDIO_DEVICE_OUT_SPEAKER_L;   // аудио-устройство вывода

    // тот же интерфейс, что вызывает сам Dolby в DapEffectContext::init
    void setEngineParam(uint32_t id, int32_t value);
};

size_t DapEffect::channelCount() const {
    int n = __builtin_popcount(mChannels & 0x3FFFu);  // OUT-биты каналов
    return n > 0 ? static_cast<size_t>(n) : 2;
}

bool DapEffect::createLegacy(int32_t session, int32_t ioHandle) {
    if (mHandle) return true;
    Aeli& a = aeli();
    ALOGI("createLegacy: aeli.ok=%d", a.ok ? 1 : 0);
    if (!a.ok) {
        ALOGI("createLegacy: загружаю AELI...");
        if (!a.load()) { ALOGE("AELI load failed"); return false; }
    }
    // Dolby-либа требует СВОЙ uuid (он отличается от прописанного в конфиге!)
    effect_uuid_t legacyUuid = toLegacyUuid(mAidlDesc.common.id.uuid);
    if (const effect_uuid_t* ru = a.realUuid()) {
        legacyUuid = *ru;
        ALOGI("использую uuid из библиотеки Dolby");
    } else {
        ALOGW("uuid библиотеки неизвестен, пробую uuid из конфига");
    }
    int32_t r = a.desc->create_effect(&legacyUuid, session, ioHandle, &mHandle);
    ALOGI("create_effect вернул %d, handle=%p", r, mHandle);
    if (r != 0) { ALOGE("create_effect -> %d", r); return false; }
    return true;
}

void DapEffect::closeLegacy() {
    if (mHandle && aeli().ok) { aeli().desc->release_effect(mHandle); }
    mHandle = nullptr;
}

bool DapEffect::pushConfig() {
    if (!mHandle) return false;
    effect_config_t cfg{};
    cfg.inputCfg.samplingRate = mSamplingRate;
    cfg.inputCfg.channels = mChannels;
    cfg.inputCfg.bufferProvider.getNextBuffer = nullptr;
    cfg.inputCfg.bufferProvider.releaseBuffer = nullptr;
    cfg.inputCfg.bufferProvider.cookie = nullptr;
    cfg.inputCfg.format = LEGACY_FORMAT_FLOAT;
    cfg.inputCfg.accessMode = EFFECT_BUFFER_ACCESS_READ;
    cfg.inputCfg.mask = EFFECT_CONFIG_ALL;
    // frameCount обязателен (как в AudioFlinger::EffectModule::setConfig)
    cfg.inputCfg.buffer.frameCount = mFrameCount ? mFrameCount : 2048;
    cfg.outputCfg = cfg.inputCfg;
    cfg.outputCfg.accessMode = EFFECT_BUFFER_ACCESS_WRITE;
    uint32_t replySize = sizeof(int32_t);
    int32_t reply = 0;
    int32_t r = (*mHandle)->command(mHandle, CMD_SET_CONFIG, sizeof(cfg), &cfg, &replySize,
                                    &reply);
    ALOGI("SET_CONFIG -> %d reply=%d (rate=%u ch=0x%x fmt=%u)", r, reply, mSamplingRate,
          mChannels, LEGACY_FORMAT_FLOAT);
    return r == 0;
}

static bool bypassRequested() {
    const char* paths[] = { "/data/vendor/dolby/dolby_bypass",
                            "/data/local/tmp/dolby_bypass", nullptr };
    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_RDONLY);
        if (fd < 0) continue;
        char b[4] = {0};
        ssize_t n = read(fd, b, sizeof(b) - 1);
        close(fd);
        if (n > 0 && b[0] == '1') return true;
    }
    return false;
}

// ===== профиль Dolby ========================================================
// Значения взяты из настоящего dax-default.xml (профиль "Dynamic"/"Music") -
// это эталонные числа самого Dolby для наушников и динамика.
static DapParamList buildAtmosProfile(bool on, bool headphone) {
    const int32_t E = on ? 1 : 0;
    DapParamList ps;
    ps.reserve(kMaxDapParams);
    auto add = [&](uint32_t id, std::initializer_list<int32_t> vals) {
        DapParam d{};
        d.id = id;
        d.n = 0;
        for (int32_t v : vals) {
            if (d.n < 8) d.v[d.n++] = v;
        }
        ps.push_back(d);
    };
    auto add1 = [&](uint32_t id, int32_t v) { add(id, { v }); };

    // --- глобальные тумблеры (совпадают для всех устройств) ---
    add1(P4('m', 'a', 'v', 'e'), E);   // mi adaptive virtualizer steering
    add1(P4('m', 'i', 'e', 'e'), E);   // mi IEQ steering
    add1(P4('m', 'd', 'l', 'e'), E);   // mi volume-leveler steering
    add1(P4('m', 'd', 'e', 'e'), E);   // mi dialog-enhancer steering
    add1(P4('m', 's', 'c', 'e'), E);   // mi surround compressor steering
    add1(P4('m', 'v', 'b', 'e'), 0);   // mi binaural steering (в XML выкл.)
    add1(P4('l', 'a', 'v', 'e'), E);   // виртуализатор (virtualizer enable)
    add1(P4('r', 'v', 's', 'e'), E);   // reverb suppression
    add1(P4('r', 'v', 's', 'a'), 2);   // reverb suppression amount
    add1(P4('d', 'v', 'm', 'e'), 0);   // volume modeler (в XML выкл.)

    // --- IEQ, диалоги, выравнивание громкости ---
    add1(P4('i', 'e', 'o', 'n'), E);   // intelligent EQ
    add1(P3('i', 'e', 'a'), headphone ? 6 : 4);
    add1(P4('d', 'e', 'o', 'n'), E);   // dialog enhancer
    add1(P3('d', 'e', 'a'), 4);
    add1(P3('d', 'e', 'd'), 0);        // ducking
    add1(P4('d', 'v', 'l', 'e'), E);   // volume leveler
    add1(P4('d', 'v', 'l', 'a'), 0);   // amount
    add1(P4('d', 'v', 'l', 'i'), -256);  // in target
    add1(P4('d', 'v', 'l', 'o'), -256);  // out target
    add1(P4('g', 'e', 'o', 'n'), E);   // graphic EQ

    // --- уровень/громкость ---
    add1(P3('p', 'l', 'b'), 0);        // calibration boost
    add1(P3('v', 'm', 'b'), headphone ? 32 : 56);  // volmax boost
    add1(P3('d', 's', 'b'), 0);        // surround boost
    add1(P4('h', 'p', 'o', 'n'), 0);   // hearing protection ВЫКЛ

    // --- объём/виртуализация ---
    add1(P4('n', 'g', 'o', 'n'), E);   // surround decoder
    add1(P4('v', 's', 's', 'd'), 4);   // headphone virtualizer source distance
    add1(P4('s', 'v', 's', 'b'), 0);   // virtualizer start band
    add(P4('h', 'v', 'r', 'c'), { 35, 32568, 11164, 5090, 0, 3, 3, 3 });
    add(P4('s', 'v', 'r', 'c'), { 35, 32568, 11164, 5090, 0, 3, 3, 3 });
    add1(P4('b', 'e', 'x', 'e'), 0);   // bass extraction (в XML выкл.)
    add1(P4('b', 'e', 'o', 'n'), 0);   // bass enhancer (в XML выкл.)
    return ps;
}

// файл-подстройка: "deam 6" / "beb 192 0" - по строке на параметр
// (/data/vendor/dolby/dolby_params.txt или /data/local/tmp/dolby_params.txt)
static bool readParamsFile(DapParamList& ps, uint64_t* sig) {
    const char* paths[] = { "/data/vendor/dolby/dolby_params.txt",
                            "/data/local/tmp/dolby_params.txt", nullptr };
    for (int i = 0; paths[i]; i++) {
        int fd = open(paths[i], O_RDONLY);
        if (fd < 0) continue;
        char buf[2048] = {0};
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) continue;
        uint64_t h = 1469598103934665603ULL;
        for (ssize_t k = 0; k < n; k++) { h ^= (uint8_t)buf[k]; h *= 1099511628211ULL; }
        if (sig) *sig = h;
        // разбор строк
        char* line = buf;
        while (line && *line) {
            char* nl = strchr(line, '\n');
            if (nl) *nl = 0;
            // пропускаем пустые/комментарии
            char* p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (*p && *p != '#') {
                char tok[16] = {0};
                int ti = 0;
                while (*p && *p != ' ' && *p != '\t' && ti < 15) tok[ti++] = *p++;
                if (ti >= 3) {
                    uint32_t id = P4(tok[0], tok[1], tok[2], ti >= 4 ? tok[3] : '\0');
                    DapParam d{};
                    d.id = id;
                    d.n = 0;
                    while (*p && d.n < 8) {
                        while (*p == ' ' || *p == '\t' || *p == ',') p++;
                        if (!*p || *p == '#') break;
                        char* end = nullptr;
                        long v = strtol(p, &end, 0);
                        if (end == p) break;
                        d.v[d.n++] = (int32_t)v;
                        p = end;
                    }
                    if (d.n == 0) { d.n = 1; d.v[0] = 0; }
                    // заменяем параметр с таким же id, иначе добавляем
                    bool replaced = false;
                    for (auto& old : ps) {
                        if (old.id == d.id) { old = d; replaced = true; break; }
                    }
                    if (!replaced && ps.size() < kMaxDapParams) ps.push_back(d);
                }
            }
            line = nl ? nl + 1 : nullptr;
        }
        return true;
    }
    if (sig) *sig = 0;
    return false;
}

// ===== передача DAP-параметров в движок Dolby ===============================
// EFFECT_CMD_SET_PARAM с paramId=0 (EFFECT_PARAM_SET_VALUES).
// Это тот же путь, которым пользуется DMS, и он применяет параметры НАПРЯМУЮ
// в DSP (без сервиса DMS).
bool DapEffect::sendSetValues(const DapParamList& ps) {
    if (!mHandle) return false;
    size_t blob = 8;  // activeDevice + numParams
    for (const auto& p : ps) blob += 12 + 4 * p.n;
    const size_t cmdSize = 16 + blob;  // 12 (effect_param_t) + 4 (paramId) + блоб
    std::vector<uint8_t> buf(cmdSize, 0);
    auto w32 = [&](size_t off, uint32_t v) { memcpy(buf.data() + off, &v, 4); };
    w32(0, 0);                        // status
    w32(4, 4);                        // psize = размер id параметра
    w32(8, static_cast<uint32_t>(blob));  // vsize
    w32(12, 0);                       // paramId = EFFECT_PARAM_SET_VALUES
    size_t o = 16;
    // ВАЖНО: activeDevice != 0 - только тогда Dolby коммитит параметры в DSP
    // (дизасм DapEffectContext::setParamValues: CBZ на поле +16 пропускает commit)
    w32(o, mDevice); o += 4;
    w32(o, static_cast<uint32_t>(ps.size())); o += 4;
    for (const auto& p : ps) {
        w32(o, mDevice); o += 4;      // deviceId (per-param, уходит в движок)
        w32(o, p.id); o += 4;
        w32(o, p.n); o += 4;
        for (uint32_t i = 0; i < p.n; i++) { w32(o, static_cast<uint32_t>(p.v[i])); o += 4; }
    }
    uint32_t replySize = 16;
    uint32_t reply[4] = { 0, 0, 0, 0 };
    int32_t r = (*mHandle)->command(mHandle, CMD_SET_PARAM, static_cast<uint32_t>(cmdSize),
                                    buf.data(), &replySize, reply);
    ALOGI("SET_VALUES: %u параметров, %u байт, устройство=0x%x -> r=%d status=%d",
          (unsigned)ps.size(), (unsigned)cmdSize, mDevice, r, (int)reply[0]);
    return r == 0;
}

void DapEffect::setEngineParam(uint32_t id, int32_t value) {
    DapParamList ps;
    DapParam d{};
    d.id = id; d.n = 1; d.v[0] = value;
    ps.push_back(d);
    sendSetValues(ps);
}

bool DapEffect::applyAtmos(bool on) {
    if (!mHandle) return false;
    DapParamList ps = buildAtmosProfile(on, mHeadphone);
    uint64_t sig = mParamsFileSig;
    bool haveFile = readParamsFile(ps, &sig);
    mParamsFileSig = sig;
    ALOGI("applyAtmos: on=%d headphone=%d params=%u file=%d", on ? 1 : 0,
          mHeadphone ? 1 : 0, (unsigned)ps.size(), haveFile ? 1 : 0);
    // крупный лог по каждому параметру - чтобы в отладке было видно, что ушло
    for (const auto& p : ps) {
        char c[5] = { (char)(p.id & 0xFF), (char)((p.id >> 8) & 0xFF),
                      (char)((p.id >> 16) & 0xFF), (char)((p.id >> 24) & 0xFF), 0 };
        ALOGI("  %s = %d (n=%u)", c, p.n ? (int)p.v[0] : -1, (unsigned)p.n);
    }
    mAmosOn = on;
    return sendSetValues(ps);
}

::ndk::ScopedAStatus DapEffect::open(const Parameter::Common& common,
                                     const std::optional<Parameter::Specific>& specific,
                                     IEffect::OpenEffectReturn* ret) {
    ALOGI("open() called: session=%d io=%d", common.session, common.ioHandle);
    std::lock_guard lg(mMutex);
    if (mState != State::INIT) {
        ALOGW("open: bad state");
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    mSession = common.session;
    // частота/каналы из common.input
    if (common.input.base.sampleRate > 0) mSamplingRate = common.input.base.sampleRate;
    {
        const auto& layout = common.input.base.channelMask;
        using Acl = ::aidl::android::media::audio::common::AudioChannelLayout;
        uint32_t mask = 0;
        if (layout.getTag() == Acl::layoutMask) {
            mask = static_cast<uint32_t>(layout.get<Acl::layoutMask>());
        } else if (layout.getTag() == Acl::indexMask) {
            mask = static_cast<uint32_t>(layout.get<Acl::indexMask>());
        }
        if (mask) {
            // Dolby ждёт ИНДЕКСНУЮ маску каналов (0x3 = стерео, 0x3F = 5.1, 0x63F = 7.1)
            // AIDL indexMask уже в этом виде; layout 0xC (FL|FR) переводим в 0x3
            uint32_t m = mask;
            if (m == 0xCu) m = 0x3u;                 // OUT_STEREO -> index stereo
            else if (m == 0x30u) m = 0xCu;           // quad?
            mChannels = m;
        }
    }
    mFrameCount = static_cast<uint32_t>(common.input.frameCount);

    if (!createLegacy(common.session, common.ioHandle))
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    if (!pushConfig())
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);

    // EFFECT_CMD_INIT - обязательная инициализация (как в AudioFlinger::EffectModule::init)
    if (mHandle) {
        uint32_t rs = sizeof(int32_t); int32_t reply = 0;
        int32_t r0 = (*mHandle)->command(mHandle, CMD_INIT, 0, nullptr, &rs, &reply);
        ALOGI("EFFECT_CMD_INIT -> %d", r0);
        // пробуем включить сразу (проверка)
        uint32_t rsE = sizeof(int32_t); int32_t replyE = 0;
        int32_t rE = (*mHandle)->command(mHandle, CMD_ENABLE, 0, nullptr, &rsE, &replyE);
        ALOGI("ENABLE(в open) -> %d reply=%d", rE, replyE);
        mLegacyEnabled = true;
        // включить функции Dolby (deon/ieon/dvle/...) - без этого движок
        // работает «прозрачно» (сам Dolby сбрасывает все тумблеры в 0 при init)
        mApplyPending = bypassRequested() ? 0 : 1;
    }


    size_t n = channelCount();
    size_t samples = (mFrameCount ? mFrameCount : 1024) * n;
    mStatusMQ = std::make_unique<StatusMQ>(16, true /* configureEventFlag */);
    mInputMQ = std::make_unique<DataMQ>(samples, false);
    mOutputMQ = std::make_unique<DataMQ>(samples, false);
    if (!mStatusMQ->isValid() || !mInputMQ->isValid() || !mOutputMQ->isValid()) {
        ALOGE("FMQ invalid");
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    mEvFlagWord = reinterpret_cast<uint32_t*>(mStatusMQ->getEventFlagWord());
    if (!mEvFlagWord) {
        ALOGE("no event flag word in status MQ");
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    ret->statusMQ = mStatusMQ->dupeDesc();
    ret->inputDataMQ = mInputMQ->dupeDesc();
    ret->outputDataMQ = mOutputMQ->dupeDesc();

    mRun = true;
    mWorker = std::thread(&DapEffect::workerLoop, this);
    ALOGI("worker thread started");

    mState = State::IDLE;
    ALOGI("open ok: session=%d io=%d rate=%u frames=%u", common.session, common.ioHandle,
          mSamplingRate, mFrameCount);
    return ::ndk::ScopedAStatus::ok();
}

void DapEffect::workerLoop() {
    uint32_t efState = 0;
    std::vector<float> in, out, orig;
    int loops = 0;
    int dbgCount = 0;
    bool bypass = false;
    ALOGI("worker: enter loop");
    while (mRun) {
        // применить профиль Dolby, если есть запрос (из open() или от тумблера)
        int pend = mApplyPending.exchange(-1);
        if (pend >= 0) {
            ALOGI("worker: применяю профиль Dolby (%s)", pend ? "ВКЛ" : "ВЫКЛ");
            applyAtmos(pend != 0);
        }
        int st = ef_wait(mEvFlagWord, kEventFlagDataMqNotEmpty, 500'000'000ULL /*500ms*/,
                         &efState);
        if (!mRun) break;
        if (st != 0 || !(efState & kEventFlagDataMqNotEmpty)) {
            if (++loops % 20 == 0) ALOGI("worker: wait timeout/state=%d", st);
            continue;
        }

        const size_t avail = mInputMQ->availableToRead();
        if (avail == 0) continue;
        if ((++loops % 25) == 0) {
            bool b = bypassRequested();
            if (b != bypass) {
                bypass = b;
                mApplyPending = b ? 0 : 1;
                ALOGI("worker: тумблер -> %s", b ? "выкл (прозрачно)" : "вкл (Dolby)");
            }
            // изменился файл-подстройка? → пере-применить профиль
            uint64_t sig = 0;
            DapParamList dummy;
            if (readParamsFile(dummy, &sig) && sig != mParamsFileSig && !bypass)
                mApplyPending = 1;
        }
        in.resize(avail);
        if (!mInputMQ->read(in.data(), avail)) continue;
        out.resize(avail);
        orig.resize(avail);
        std::copy(in.begin(), in.end(), orig.begin());   // снимок ДО обработки
        // (Dolby может писать прямо во входной буфер - in-place!)

        audio_buffer_t bin{}, bout{};
        bin.audio.f32 = in.data(); bin.frameCount = avail / channelCount();
        bout.audio.f32 = out.data(); bout.frameCount = bin.frameCount;

        IEffect::Status status{};
        status.status = 0;  // OK
        if (mLegacyEnabled && mHandle && !bypass) {
            int32_t r = (*mHandle)->process(mHandle, &bin, &bout);
            if (r != 0) {
                ALOGW("process -> %d, fallback to passthrough", r);
                std::copy(in.begin(), in.end(), out.begin());
            } else if ((++dbgCount % 25) == 0) {
                // реально ли Dolby меняет сигнал? сравниваем СНИМОК входа с выходом
                double si = 0.0, so = 0.0, maxd = 0.0;
                size_t ndiff = 0;
                for (size_t i = 0; i < avail; i++) {
                    double a = orig[i], b = out[i];
                    si += a * a;
                    so += b * b;
                    double d = b - a;
                    if (d < 0) d = -d;
                    if (d > maxd) maxd = d;
                    if (d > 1e-7) ndiff++;
                }
                si = std::sqrt(si / static_cast<double>(avail ? avail : 1));
                so = std::sqrt(so / static_cast<double>(avail ? avail : 1));
                ALOGI("DSP: rms_in=%.6f rms_out=%.6f gain=%+.2fdB maxdiff=%.6f diff=%zu/%zu%s",
                       si, so, 20.0 * std::log10((so + 1e-12) / (si + 1e-12)), maxd, ndiff,
                       avail, bypass ? " [bypass]" : "");
            }
        } else {
            std::copy(in.begin(), in.end(), out.begin());  // bypass
        }
        status.fmqConsumed = static_cast<int32_t>(avail);
        status.fmqProduced = static_cast<int32_t>(avail);
        if (!mOutputMQ->write(out.data(), avail)) { ALOGE("output write fail"); }
        if (!mStatusMQ->write(&status, 1)) { ALOGE("status write fail"); }
        // разбудить клиента, читающего statusMQ через readBlocking (бит FMQ_NOT_EMPTY)
        ef_wake(mEvFlagWord, kFmqNotEmpty);
    }
}

::ndk::ScopedAStatus DapEffect::close() {
    ALOGI("close() called");
    std::lock_guard lg(mMutex);
    if (mState == State::INIT)
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    mRun = false;
    if (mEvFlagWord) ef_wake(mEvFlagWord, kEventFlagDataMqNotEmpty);
    if (mWorker.joinable()) mWorker.join();
    mEvFlagWord = nullptr;
    mStatusMQ.reset(); mInputMQ.reset(); mOutputMQ.reset();
    closeLegacy();
    mState = State::INIT;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus DapEffect::getDescriptor(Descriptor* d) {
    *d = mAidlDesc;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus DapEffect::command(CommandId id) {
    ALOGI("command id=%d", static_cast<int>(id));
    std::lock_guard lg(mMutex);
    uint32_t replySize = sizeof(int32_t);
    int32_t reply = 0;
    switch (id) {
        case CommandId::START:
            if (mHandle && !mLegacyEnabled) {
                uint32_t rs = replySize;
                int32_t re = (*mHandle)->command(mHandle, CMD_ENABLE, 0, nullptr, &rs, &reply);
                ALOGI("ENABLE -> %d (reply=%d)", re, reply);
                mLegacyEnabled = true;
            }
            mState = State::PROCESSING;
            break;
        case CommandId::STOP:
            if (mHandle && mLegacyEnabled) {
                uint32_t rs = replySize;
                (*mHandle)->command(mHandle, CMD_DISABLE, 0, nullptr, &rs, &reply);
                mLegacyEnabled = false;
            }
            mState = State::IDLE;
            break;
        case CommandId::RESET:
            if (mHandle) {
                uint32_t rs = replySize;
                (*mHandle)->command(mHandle, CMD_RESET, 0, nullptr, &rs, &reply);
                mApplyPending = bypassRequested() ? 0 : 1;   // Dolby мог сбросить профиль
            }
            break;
        default:
            return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus DapEffect::getState(State* s) {
    std::lock_guard lg(mMutex);
    *s = mState;
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus DapEffect::setParameter(const Parameter& p) {
    std::lock_guard lg(mMutex);
    switch (p.getTag()) {
        case Parameter::deviceDescription: {
            if (mHandle) {
                uint32_t dev = AUDIO_DEVICE_OUT_SPEAKER_L;
                bool hp = false;
                const auto& devs = p.get<Parameter::deviceDescription>();
                if (!devs.empty()) {
                    using ADT = ::aidl::android::media::audio::common::AudioDeviceType;
                    const ADT ty = devs[0].type;
                    const std::string& cn = devs[0].connection;
                    const bool bt = cn == "BLUETOOTH_A2DP" || cn == "BLUETOOTH_SCO" ||
                                    cn == "BLUETOOTH_LE" || cn == "BLE";
                    const bool usb = cn == "USB";
                    if (ty == ADT::OUT_HEADPHONE || ty == ADT::OUT_HEADSET ||
                        ty == ADT::OUT_HEARING_AID) { dev = 0x8; hp = true; }
                    if (ty == ADT::OUT_DEVICE || ty == ADT::OUT_ACCESSORY ||
                        ty == ADT::OUT_DOCK || ty == ADT::OUT_CARKIT || usb) {
                        dev = 0x4000; hp = true;
                    }
                    if (bt) { dev = 0x80; hp = true; }
                    ALOGI("SET_DEVICE: type=%d conn='%s' -> 0x%x headphone=%d", (int)ty,
                          cn.c_str(), dev, hp ? 1 : 0);
                }
                if (hp != mHeadphone || dev != mDevice) {
                    mHeadphone = hp;
                    mDevice = dev;
                    mApplyPending = 1;   // параметры привязаны к устройству - пере-применяем
                } else {
                    mDevice = dev;
                }
                uint32_t rs = sizeof(int32_t); int32_t reply = 0;
                (*mHandle)->command(mHandle, CMD_SET_DEVICE, sizeof(dev), &dev, &rs, &reply);
            }
            return ::ndk::ScopedAStatus::ok();
        }
        case Parameter::volumeStereo: {
            const auto& v = p.get<Parameter::volumeStereo>();
            mVolume = { static_cast<int32_t>(v.left * VOLUME_U24_8_ONE),
                        static_cast<int32_t>(v.right * VOLUME_U24_8_ONE) };
            if (mHandle) {
                uint32_t rs = sizeof(uint32_t); uint32_t reply = 0;
                int32_t vol[2] = { mVolume[0], mVolume[1] };
                (*mHandle)->command(mHandle, CMD_SET_VOLUME, sizeof(vol), vol, &rs, &reply);
            }
            return ::ndk::ScopedAStatus::ok();
        }
        case Parameter::mode: {
            // AudioMode -> legacy audio_mode_t почти идентичны
            if (mHandle) {
                int32_t mode = static_cast<int32_t>(p.get<Parameter::mode>());
                uint32_t rs = sizeof(int32_t); int32_t reply = 0;
                (*mHandle)->command(mHandle, CMD_SET_AUDIO_MODE, sizeof(mode), &mode, &rs,
                                    &reply);
            }
            return ::ndk::ScopedAStatus::ok();
        }
        case Parameter::common: {
            // конфиг мог измениться
            const auto& c = p.get<Parameter::common>();
            if (c.input.base.sampleRate > 0) mSamplingRate = c.input.base.sampleRate;
            pushConfig();
            if (mHandle && mState != State::INIT)
                mApplyPending = bypassRequested() ? 0 : 1;
            return ::ndk::ScopedAStatus::ok();
        }
        default:
            return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
}

::ndk::ScopedAStatus DapEffect::getParameter(const Parameter::Id&, Parameter*) {
    return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

::ndk::ScopedAStatus DapEffect::reopen(IEffect::OpenEffectReturn* ret) {
    std::lock_guard lg(mMutex);
    if (!mStatusMQ) return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    ret->statusMQ = mStatusMQ->dupeDesc();
    ret->inputDataMQ = mInputMQ->dupeDesc();
    ret->outputDataMQ = mOutputMQ->dupeDesc();
    return ::ndk::ScopedAStatus::ok();
}

// ============================ фабрика =======================================
}  // namespace

extern "C" __attribute__((visibility("default"))) binder_exception_t createEffect(
        const AudioUuid* uuid, std::shared_ptr<IEffect>* instanceSpp) {
    ALOGI("createEffect uuid=%08x", uuid ? uuid->timeLow : 0);
    if (!uuid || !instanceSpp) return EX_ILLEGAL_ARGUMENT;
    if (!uuidEquals(*uuid, kDapUuid)) return EX_ILLEGAL_ARGUMENT;
    Descriptor d;
    d.common.id.type = kDapTypeUuid;
    d.common.id.uuid = kDapUuid;
    d.common.name = "Dolby Audio Processing";
    d.common.implementor = "Dolby";
    d.common.flags.type = aidl::android::hardware::audio::effect::Flags::Type::INSERT;
    d.common.flags.insert = aidl::android::hardware::audio::effect::Flags::Insert::ANY;
    d.common.flags.volume = aidl::android::hardware::audio::effect::Flags::Volume::CTRL;
    *instanceSpp = ::ndk::SharedRefBase::make<DapEffect>(d);
    ALOGI("createEffect: instance created");
    return EX_NONE;
}

extern "C" __attribute__((visibility("default"))) binder_exception_t queryEffect(
        const AudioUuid* uuid, Descriptor* _aidl_return) {
    ALOGI("queryEffect called");
    if (!uuid || !_aidl_return) return EX_ILLEGAL_ARGUMENT;
    if (!uuidEquals(*uuid, kDapUuid)) return EX_ILLEGAL_ARGUMENT;
    // статический дескриптор: не трогаем Dolby на этапе регистрации
    _aidl_return->common.id.type = kDapTypeUuid;
    _aidl_return->common.id.uuid = kDapUuid;
    _aidl_return->common.name = "Dolby Audio Processing";
    _aidl_return->common.implementor = "Dolby";
    _aidl_return->common.flags.type =
        aidl::android::hardware::audio::effect::Flags::Type::INSERT;
    _aidl_return->common.flags.insert =
        aidl::android::hardware::audio::effect::Flags::Insert::ANY;
    _aidl_return->common.flags.volume =
        aidl::android::hardware::audio::effect::Flags::Volume::CTRL;
    ALOGI("queryEffect ok");
    return EX_NONE;
}

extern "C" __attribute__((visibility("default"))) binder_exception_t destroyEffect(
        const std::shared_ptr<IEffect>& instanceSp) {
    State state;
    if (instanceSp && instanceSp->getState(&state).isOk() && state != State::INIT)
        return EX_ILLEGAL_STATE;
    return EX_NONE;
}

// AELI грузим лениво (при первом createEffect), чтобы не рисковать на старте HAL
__attribute__((constructor)) static void shimInit() {
    ALOGI("libdolbyaidlshim loaded (lazy AELI)");
}
