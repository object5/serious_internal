#include "pch.h"
#include "hack.h"
#include <Windows.h>
#include <cstring>

namespace hack {

static bool god = false;


template <typename T>
static bool readMem(uintptr_t addr, T& v)
{
    if (!addr) return false;
    SIZE_T done = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, &v, sizeof v, &done)
        && done == sizeof v;
}

static bool readBuf(uintptr_t addr, void* buf, SIZE_T size)
{
    if (!addr || !buf || !size) return false;
    SIZE_T done = 0;
    return ReadProcessMemory(GetCurrentProcess(), (LPCVOID)addr, buf, size, &done)
        && done == size;
}

static bool writeMem(uintptr_t addr, const void* buf, SIZE_T size)
{
    if (!addr || !buf || !size) return false;
    SIZE_T done = 0;
    return WriteProcessMemory(GetCurrentProcess(), (LPVOID)addr, buf, size, &done)
        && done == size;
}

static bool readText(uintptr_t addr, char* out, int len)
{
    char buf[72];
    if (len > (int)sizeof buf || !readBuf(addr, buf, len - 1)) return false;
    buf[len - 1] = 0;
    int i = 0;
    while (i < len - 1 && buf[i]) {
        if (buf[i] < 32 || buf[i] > 126) return false;
        ++i;
    }
    if (i < 1 || i > 64) return false;
    memcpy(out, buf, (size_t)i + 1);
    return true;
}

static uintptr_t entitiesBase()
{
    HMODULE h = GetModuleHandleA("EntitiesMP.dll");
    if (!h) h = GetModuleHandleA("Entities.dll");
    return (uintptr_t)h;
}

uintptr_t GetLocalPlayer()
{
    uintptr_t base = entitiesBase();
    if (!base) return 0;
    uintptr_t player = 0;
    readMem(base + kPlayerPtrOffset, player);
    return player;
}

bool IsPlayerValid()
{
    return GetLocalPlayer() != 0;
}

float GetHealth()
{
    uintptr_t player = GetLocalPlayer();
    float hp = -1.0f;
    if (player) readMem(player + kHpOffset, hp);
    return hp;
}

bool SetHealth(float hp)
{
    uintptr_t player = GetLocalPlayer();
    return player && writeMem(player + kHpOffset, &hp, sizeof hp);
}

void SetGodMode(bool on) { god = on; }
bool IsGodMode() { return god; }

uintptr_t GetWorld()
{
    uintptr_t player = GetLocalPlayer();
    uintptr_t world = 0;
    if (player) readMem(player + kWorldOffset, world);
    return world;
}

#ifdef _WIN64
static const uintptr_t kArrOff = 8, kUsedOff = 16;
#else
static const uintptr_t kArrOff = 4, kUsedOff = 8;
#endif

static bool entityInWorld(uintptr_t e, uintptr_t world)
{
    if (!e || !world) return false;
    uintptr_t w = 0;
    int id = -1;
    return readMem(e + kWorldOffset, w) && w == world
        && readMem(e + kIdOffset, id) && id >= 0 && id < 1000000;
}

static bool containerLooksOk(uintptr_t c, uintptr_t world)
{
    int used = 0;
    uintptr_t arr = 0;
    if (!readMem(c + kUsedOff, used) || used <= 0 || used > 100000) return false;
    if (!readMem(c + kArrOff, arr) || !arr) return false;
    uintptr_t e0 = 0, e1 = 0;
    if (!readMem(arr, e0)) return false;
    e1 = (used > 1) ? 0 : e0;
    if (used > 1 && !readMem(arr + sizeof(uintptr_t), e1)) return false;
    return entityInWorld(e0, world) && entityInWorld(e1, world);
}

uintptr_t FindEntityContainer()
{
    uintptr_t world = GetWorld();
    if (!world) return 0;

    static uintptr_t cached = 0;
    if (cached && containerLooksOk(cached, world)) return cached;
    cached = 0;

    for (uintptr_t c = world; c < world + 0x800; c += 4) {
        int count = 0;
        if (!readMem(c, count) || count <= 0 || count > 100000) continue;
        int used = 0;
        uintptr_t arr = 0;
        if (!readMem(c + kUsedOff, used) || used <= 0 || used > count) continue;
        if (!readMem(c + kArrOff, arr) || !arr) continue;
        uintptr_t e0 = 0, e1 = 0;
        if (!readMem(arr, e0) || !readMem(arr + sizeof(uintptr_t), e1)) continue;
        if (!entityInWorld(e0, world) || !entityInWorld(e1, world)) continue;
        cached = c;
        return cached;
    }
    return 0;
}

int GetEntityCount()
{
    uintptr_t c = FindEntityContainer();
    int used = 0;
    if (!c || !readMem(c + kUsedOff, used) || used <= 0 || used > 100000) return 0;
    return used;
}

uintptr_t GetEntity(int index)
{
    uintptr_t c = FindEntityContainer();
    if (!c || index < 0) return 0;
    uintptr_t arr = 0, e = 0;
    if (!readMem(c + kArrOff, arr)) return 0;
    readMem(arr + (uintptr_t)index * sizeof(uintptr_t), e);
    return e;
}

int GetEntityId(uintptr_t entity)
{
    int id = -1;
    if (!entity || !readMem(entity + kIdOffset, id) || id < 0 || id >= 1000000) return -1;
    return id;
}

float GetEntityHp(uintptr_t entity)
{
    float hp = -1.0f;
    if (entity) readMem(entity + kHpOffset, hp);
    return hp;
}

bool SetEntityHp(uintptr_t entity, float hp)
{
    return entity && writeMem(entity + kHpOffset, &hp, sizeof hp);
}

unsigned GetEntityFlags(uintptr_t entity)
{
    unsigned fl = 0;
    if (entity) {
#ifdef _WIN64
        readMem(entity + 0x14, fl);
#else
        readMem(entity + 0x10, fl);
#endif
    }
    return fl;
}

int GetEntityRenderType(uintptr_t entity)
{
    int rt = 0;
    if (entity) {
#ifdef _WIN64
        readMem(entity + 0x08, rt);
#else
        readMem(entity + 0x04, rt);
#endif
    }
    return rt;
}

bool GetEntityPos(uintptr_t entity, float out[3])
{
    if (!entity || !out) return false;
#ifdef _WIN64
    return readBuf(entity + 0x24, out, 3 * sizeof(float));
#else
    return readBuf(entity + 0x28, out, 3 * sizeof(float));
#endif
}



struct ClassOffs { uintptr_t pec, name, base; };
static ClassOffs g_coffs{ 0, 0, 0 };
static bool g_coffsDone = false;

static int chainDepth(uintptr_t entity, uintptr_t pecOff, uintptr_t nameOff, uintptr_t baseOff)
{
    uintptr_t pec = 0;
#ifdef _WIN64
    if (!readMem(entity + 0x60, pec) || !pec) return 0;
#else
    if (!readMem(entity + 0x5C, pec) || !pec) return 0;
#endif
    uintptr_t pdec = 0;
    if (!readMem(pec + pecOff, pdec)) return 0;
    int depth = 0;
    char tmp[72];
    while (depth < 8 && pdec) {
        uintptr_t nm = 0;
        if (!readMem(pdec + nameOff, nm) || !readText(nm, tmp, sizeof tmp)) break;
        if (!readMem(pdec + baseOff, pdec)) break;
        ++depth;
    }
    return depth;
}

static bool resolveClassOffsets(uintptr_t hint)
{
    if (g_coffsDone) return g_coffs.pec != 0;
    uintptr_t test[6];
    int testN = 0;
    auto addTest = [&](uintptr_t e) {
        if (!e || testN >= 6) return;
        for (int i = 0; i < testN; ++i) if (test[i] == e) return;
        if (e != hint && !(GetEntityFlags(e) & 8)) return; 
        test[testN++] = e;
    };
    addTest(hint);
    addTest(GetLocalPlayer());
    int n = GetEntityCount();
    for (int i = 0; i < n && testN < 6; ++i) addTest(GetEntity(i));
    if (testN < 2) return false;
#ifdef _WIN64
    static const uintptr_t pecC[] = { 16, 24, 32, 40, 48, 56 };
    static const uintptr_t nmC[] = { 40, 48, 56 };
    static const uintptr_t bsC[] = { 64, 72, 80 };
#else
    static const uintptr_t pecC[] = { 12, 16, 20, 24, 28, 32 };
    static const uintptr_t nmC[] = { 20, 24, 28 };
    static const uintptr_t bsC[] = { 32, 36, 40 };
#endif
    int best = 0;
    for (uintptr_t po : pecC) for (uintptr_t no : nmC) for (uintptr_t bo : bsC) {
        int score = 0;
        for (int i = 0; i < testN; ++i) {
            int d = chainDepth(test[i], po, no, bo);
            if (d < 2) { score = -1; break; }
            score += d;
        }
        if (score > best) { best = score; g_coffs = { po, no, bo }; }
    }
    g_coffsDone = true;
    return g_coffs.pec != 0;
}


struct ClassInfo { uintptr_t entity, pec; char name[72]; bool enemy; };
static ClassInfo g_classes[512];
static int g_classN = 0;

static const ClassInfo* classOf(uintptr_t e)
{
    if (!e || !resolveClassOffsets(e)) return nullptr;
#ifdef _WIN64
    const uintptr_t kPecOff = 0x60;
#else
    const uintptr_t kPecOff = 0x5C;
#endif
    uintptr_t pec = 0;
    if (!readMem(e + kPecOff, pec) || !pec) return nullptr;
    for (int i = 0; i < g_classN; ++i)
        if (g_classes[i].entity == e && g_classes[i].pec == pec) return &g_classes[i];

    ClassInfo ci{};
    ci.entity = e;
    ci.pec = pec;
    uintptr_t pdec = 0;
    if (readMem(pec + g_coffs.pec, pdec)) {
        for (int lv = 0; lv < 8 && pdec; ++lv) {
            uintptr_t nm = 0;
            char buf[72];
            if (!readMem(pdec + g_coffs.name, nm) || !readText(nm, buf, sizeof buf)) break;
            if (lv == 0) memcpy(ci.name, buf, sizeof ci.name);
            if (strcmp(buf, "Enemy Base") == 0) ci.enemy = true;
            if (!readMem(pdec + g_coffs.base, pdec)) break;
        }
    }
    if (!ci.name[0]) return nullptr;
    if (g_classN >= 512) g_classN = 0; 
    g_classes[g_classN] = ci;
    return &g_classes[g_classN++];
}

int GetClassChain(uintptr_t entity, char out[][72], int maxDepth)
{
    if (!entity || !out || maxDepth <= 0 || !resolveClassOffsets(entity)) return 0;
#ifdef _WIN64
    const uintptr_t kPecOff = 0x60;
#else
    const uintptr_t kPecOff = 0x5C;
#endif
    uintptr_t pec = 0;
    if (!readMem(entity + kPecOff, pec) || !pec) return 0;
    uintptr_t pdec = 0;
    if (!readMem(pec + g_coffs.pec, pdec)) return 0;
    int depth = 0;
    while (depth < maxDepth && pdec) {
        uintptr_t nm = 0;
        if (!readMem(pdec + g_coffs.name, nm) || !readText(nm, out[depth], 72)) break;
        if (!readMem(pdec + g_coffs.base, pdec)) break;
        ++depth;
    }
    return depth;
}

bool IsEnemy(uintptr_t entity)
{
    const ClassInfo* ci = classOf(entity);
    return ci && ci->enemy;
}

bool GetEntityClassName(uintptr_t entity, char* out, int outLen)
{
    const ClassInfo* ci = classOf(entity);
    if (!ci || !out) return false;
    size_t L = strlen(ci->name) + 1;
    if ((int)L > outLen) return false;
    memcpy(out, ci->name, L);
    return true;
}



struct PropCache { uintptr_t pec; int off; };
static PropCache g_propCache[64];
static int g_propN = 0;

static int findEnemyProp(uintptr_t entity, uintptr_t pec, uintptr_t pdec)
{
    for (int i = 0; i < g_propN; ++i)
        if (g_propCache[i].pec == pec) return g_propCache[i].off;
    int found = -1;
#ifdef _WIN64
    const uintptr_t stride = 48, offName = 24, offOff = 20, offType = 0;
#else
    const uintptr_t stride = 32, offName = 16, offOff = 12, offType = 0;
#endif
    uintptr_t player = GetLocalPlayer();
    for (int pass = 0; pass < 2 && found < 0; ++pass) {
        uintptr_t lvl = pdec;
        for (int depth = 0; depth < 8 && lvl; ++depth) {
            uintptr_t arr = 0;
            int ct = 0;
#ifdef _WIN64
            if (!readMem(lvl + 0, arr) || !readMem(lvl + 8, ct)) break;
            uintptr_t base = 0;
            readMem(lvl + g_coffs.base, base);
#else
            if (!readMem(lvl + 0, arr) || !readMem(lvl + 4, ct)) break;
            uintptr_t base = 0;
            readMem(lvl + 36, base);
#endif
            if (ct > 0 && ct < 5000 && arr) {
                char nm[72];
                for (int i = 0; i < ct; ++i) {
                    uintptr_t pr = arr + (uintptr_t)i * stride;
                    int tp = -1;
                    if (!readMem(pr + offType, tp) || tp != 7) continue;
                    if (pass == 0) {
                        uintptr_t name = 0;
                        if (!readMem(pr + offName, name) || !readText(name, nm, sizeof nm)) continue;
                        if (strcmp(nm, "Enemy") != 0) continue;
                    }
                    int off = -1;
                    if (!readMem(pr + offOff, off) || off < 0 || off > 4096) continue;
                    if (pass == 1) {
                        if (!player) continue;
                        uintptr_t v = 0;
                        if (!readMem(entity + (uintptr_t)off, v) || v != player) continue;
                    }
                    found = off;
                    break;
                }
            }
            lvl = base;
        }
    }
    if (found >= 0 && g_propN < 64) {
        g_propCache[g_propN].pec = pec;
        g_propCache[g_propN].off = found;
        ++g_propN;
    }
    return found;
}

uintptr_t GetEnemyTarget(uintptr_t entity)
{
    if (!entity || !resolveClassOffsets(entity)) return 0;
#ifdef _WIN64
    const uintptr_t kPecOff = 0x60;
#else
    const uintptr_t kPecOff = 0x5C;
#endif
    uintptr_t pec = 0, pdec = 0;
    if (!readMem(entity + kPecOff, pec) || !pec) return 0;
    if (!readMem(pec + g_coffs.pec, pdec) || !pdec) return 0;
    int off = findEnemyProp(entity, pec, pdec);
    if (off < 0) return 0;
    uintptr_t target = 0;
    readMem(entity + (uintptr_t)off, target);
    return target;
}

int DumpEntityPtrProps(uintptr_t entity, char names[][72], uintptr_t vals[], int max)
{
    if (!entity || !names || !vals || max <= 0 || !resolveClassOffsets(entity)) return 0;
#ifdef _WIN64
    const uintptr_t kPecOff = 0x60, stride = 48, offName = 24, offOff = 20, offType = 0;
#else
    const uintptr_t kPecOff = 0x5C, stride = 32, offName = 16, offOff = 12, offType = 0;
#endif
    uintptr_t pec = 0, pdec = 0;
    if (!readMem(entity + kPecOff, pec) || !pec) return 0;
    if (!readMem(pec + g_coffs.pec, pdec)) return 0;
    int out = 0;
    for (int lv = 0; lv < 8 && pdec && out < max; ++lv) {
        uintptr_t arr = 0, base = 0;
        int ct = 0;
#ifdef _WIN64
        if (!readMem(pdec + 0, arr) || !readMem(pdec + 8, ct)) break;
        readMem(pdec + g_coffs.base, base);
#else
        if (!readMem(pdec + 0, arr) || !readMem(pdec + 4, ct)) break;
        readMem(pdec + 36, base);
#endif
        if (ct <= 0 || ct >= 5000 || !arr) break;
        for (int i = 0; i < ct && out < max; ++i) {
            uintptr_t pr = arr + (uintptr_t)i * stride;
            int tp = -1, off = -1;
            uintptr_t nm = 0, v = 0;
            if (!readMem(pr + offType, tp) || tp != 7) continue;
            if (!readMem(pr + offOff, off) || off < 0 || off > 4096) continue;
            if (!readMem(pr + offName, nm) || !readText(nm, names[out], 72)) continue;
            readMem(entity + (uintptr_t)off, v);
            vals[out] = v;
            ++out;
        }
        pdec = base;
    }
    return out;
}

}
