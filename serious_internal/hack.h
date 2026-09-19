#pragma once

#include <cstdint>

namespace hack {
 
    static const uintptr_t kPlayerPtrOffset = 0x001CA570;
#ifdef _WIN64
    static const uintptr_t kHpOffset = 0x110;
#else
    static const uintptr_t kHpOffset = 0xCC;
#endif

    uintptr_t GetLocalPlayer();
    bool      IsPlayerValid();
    float     GetHealth();
    bool      SetHealth(float hp);

    void      SetGodMode(bool on);
    bool      IsGodMode();
    void      SetRapidFire(bool on);
    bool      IsRapidFire();
    void      RapidFireTick();

#ifdef _WIN64
    static const uintptr_t kWorldOffset = 0xA8;  // en_pwoWorld 
    static const uintptr_t kIdOffset = 0x20;     // en_ulID 
#else
    static const uintptr_t kWorldOffset = 0x8C;  // en_pwoWorld 
    static const uintptr_t kIdOffset = 0x1C;     // en_ulID 
#endif

    uintptr_t GetWorld();                        // [player + kWorldOffset]
    uintptr_t FindEntityContainer();             // CDynamicContainer
    int       GetEntityCount();                  // used
    uintptr_t GetEntity(int index);              // CEntity*
    int       GetEntityId(uintptr_t entity);     // en_ulID
    float     GetEntityHp(uintptr_t entity);     // [entity+0x110]
    bool      SetEntityHp(uintptr_t entity, float hp);

    uintptr_t GetEnemyTarget(uintptr_t entity);
    uintptr_t GetRayHit();
    int       DumpEntityPtrProps(uintptr_t entity, char names[][72], uintptr_t vals[], int max);
    unsigned  GetEntityFlags(uintptr_t entity);  // en_ulFlags 
    int       GetEntityRenderType(uintptr_t entity); // en_RenderType
    bool      GetEntityClassName(uintptr_t entity, char* out, int outLen);
    bool      IsEnemy(uintptr_t entity);
    int       GetClassChain(uintptr_t entity, char out[][72], int maxDepth);
    bool      GetEntityPos(uintptr_t entity, float out[3]); // en_plPlacement
    bool      GetEntityBox(uintptr_t entity, float mins[3], float maxs[3]); // en_boxSpatialClassification (object space)
    bool      GetEntityMatrix(uintptr_t entity, float m[9]); // en_mRotation

    bool      GetViewAngles(float& yaw, float& pitch); 
    bool      SetViewAngles(float yaw, float pitch);   
    bool      SetViewAnglesBody(float yaw, float pitch); 
    bool      GetEyePos(float out[3]);                 
}