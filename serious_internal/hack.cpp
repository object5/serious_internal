#include "hack.h"
#include <Windows.h>
#include <cstring>
#include <cstdio>

namespace hack {

	static bool god = false;

	template <typename T>
	static __forceinline bool readMem(uintptr_t addr, T& v)
	{
		if (!addr) return false;
		__try {
			memcpy(&v, (const void*)addr, sizeof v);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			v = T{};
			return false;
		}
	}

	static __forceinline bool readBuf(uintptr_t addr, void* buf, SIZE_T size)
	{
		if (!addr || !buf || !size) return false;
		__try {
			memcpy(buf, (const void*)addr, size);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return false;
		}
	}

	static __forceinline bool writeMem(uintptr_t addr, const void* buf, SIZE_T size)
	{
		if (!addr || !buf || !size) return false;
		__try {
			memcpy((void*)addr, buf, size);
			return true;
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			DWORD old = 0;
			if (!VirtualProtect((LPVOID)addr, size, PAGE_EXECUTE_READWRITE, &old))
				return false;
			__try {
				memcpy((void*)addr, buf, size);
			} __except (EXCEPTION_EXECUTE_HANDLER) {
				DWORD tmp = 0;
				VirtualProtect((LPVOID)addr, size, old, &tmp);
				return false;
			}
			DWORD tmp = 0;
			VirtualProtect((LPVOID)addr, size, old, &tmp);
			return true;
		}
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

#ifdef _WIN64
	static uintptr_t entitiesBase()
	{
		HMODULE h = GetModuleHandleA("EntitiesMP.dll");
		if (!h) h = GetModuleHandleA("Entities.dll");
		return (uintptr_t)h;
	}
#endif

#ifndef _WIN64
	static uintptr_t scanEngine(const unsigned char* pat, const char* mask)
	{
		HMODULE h = GetModuleHandleA("Engine.dll");
		if (!h) return 0;
		PIMAGE_DOS_HEADER dos = (PIMAGE_DOS_HEADER)h;
		if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
		PIMAGE_NT_HEADERS nt = (PIMAGE_NT_HEADERS)((uintptr_t)h + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
		uintptr_t base = (uintptr_t)h;
		uintptr_t end = base + nt->OptionalHeader.SizeOfImage;
		size_t n = strlen(mask);
		MEMORY_BASIC_INFORMATION mi{};
		for (uintptr_t p = base; p < end;) {
			if (!VirtualQuery((LPCVOID)p, &mi, sizeof mi)) break;
			uintptr_t q0 = (uintptr_t)mi.BaseAddress;
			uintptr_t q1 = q0 + mi.RegionSize;
			if (q0 < base) q0 = base;
			if (q1 > end) q1 = end;
			DWORD pr = mi.Protect & 0xFF;
			bool rd = (mi.State == MEM_COMMIT) && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD))
				&& (pr == PAGE_READONLY || pr == PAGE_READWRITE || pr == PAGE_EXECUTE_READ
					|| pr == PAGE_EXECUTE_READWRITE || pr == PAGE_EXECUTE_WRITECOPY);
			if (rd && q1 > q0 + n) {
				for (uintptr_t a = q0; a + n <= q1; ++a) {
					bool ok = true;
					for (size_t i = 0; i < n; ++i) {
						if (mask[i] == 'x' && ((unsigned char*)a)[i] != pat[i]) { ok = false; break; }
					}
					if (ok) return a;
				}
			}
			p = (uintptr_t)mi.BaseAddress + mi.RegionSize;
		}
		return 0;
	}

	static uintptr_t g_pNetworkAddr = 0;

	static uintptr_t steamLocalPlayer()
	{
		if (!g_pNetworkAddr) {
			static const unsigned char pat[] = { 0x8B, 0x0D, 0, 0, 0, 0, 0x83, 0xC4, 0x08, 0xE8, 0, 0, 0, 0, 0x85, 0xC0 };
			uintptr_t m = scanEngine(pat, "xx????xxxx????xx");
			if (!m) return 0;
			uintptr_t disp = 0;
			if (!readMem(m + 2, disp) || !disp) return 0;
			g_pNetworkAddr = disp;
		}
		uintptr_t net = 0, ses = 0, arr = 0, pen = 0;
		int active = 0;
		if (!readMem(g_pNetworkAddr, net) || !net) return 0;
		if (!readMem(net + 0x20, ses) || !ses) return 0;
		if (!readMem(ses + 0x4, arr) || !arr) return 0;
		if (!readMem(arr + 0x0, active) || !active) return 0;
		if (!readMem(arr + 0x4, pen) || !pen) return 0;
		return pen;
	}
#endif

	uintptr_t GetLocalPlayer()
	{
#ifdef _WIN64
		uintptr_t base = entitiesBase();
		if (!base) return 0;
		uintptr_t player = 0;
		readMem(base + kPlayerPtrOffset, player);
		return player;
#else
		uintptr_t p = steamLocalPlayer();
		if (!p || GetEntityId(p) < 0) return 0;
		uintptr_t w = 0;
		readMem(p + kWorldOffset, w);
		if (!w) return 0;
		return p;
#endif
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
		return readBuf(entity + 0x20, out, 3 * sizeof(float));
#endif
	}

	bool GetEntityMatrix(uintptr_t entity, float m[9])
	{
		if (!entity || !m) return false;
#ifdef _WIN64
		return readBuf(entity + 0x3C, m, 9 * sizeof(float));
#else
		return readBuf(entity + 0x38, m, 9 * sizeof(float));
#endif
	}

	bool GetEntityBox(uintptr_t entity, float mins[3], float maxs[3])
	{
		if (!entity || !mins || !maxs) return false;
		float box[6];
#ifdef _WIN64
		if (!readBuf(entity + 0x84, box, sizeof box)) return false;
#else
		if (!readBuf(entity + 0x70, box, sizeof box)) return false;
#endif
		float dx = box[3] - box[0], dy = box[4] - box[1], dz = box[5] - box[2];
		if (dx < 0.05f || dx > 100.0f) return false;
		if (dy < 0.05f || dy > 100.0f) return false;
		if (dz < 0.05f || dz > 100.0f) return false;
		memcpy(mins, box, 3 * sizeof(float));
		memcpy(maxs, box + 3, 3 * sizeof(float));
		return true;
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

	static bool rapid = false;
	void SetRapidFire(bool on) { rapid = on; }
	bool IsRapidFire() { return rapid; }

	static uintptr_t g_wpnEntity = 0;
	static uintptr_t g_wpnPec = 0;
	static int g_wpnOff = -1;

	static uintptr_t findWeaponsEntity(uintptr_t player)
	{
		if (!player || !resolveClassOffsets(player)) return 0;
#ifdef _WIN64
		const uintptr_t kPecOff = 0x60, stride = 48, offName = 24, offOff = 20, offType = 0;
#else
		const uintptr_t kPecOff = 0x5C, stride = 32, offName = 16, offOff = 12, offType = 0;
#endif
		uintptr_t pec = 0, pdec = 0;
		if (!readMem(player + kPecOff, pec) || !pec) return 0;
		if (!readMem(pec + g_coffs.pec, pdec) || !pdec) return 0;
		if (pec == g_wpnPec && g_wpnEntity && g_wpnOff >= 0) {
			uintptr_t cur = 0;
			if (readMem(player + (uintptr_t)g_wpnOff, cur) && cur == g_wpnEntity
				&& GetEntityId(g_wpnEntity) >= 0)
				return g_wpnEntity;
		}
		g_wpnEntity = 0;
		g_wpnPec = 0;
		g_wpnOff = -1;
		for (int pass = 0; pass < 2; ++pass) {
			uintptr_t lvl = pdec;
			for (int depth = 0; depth < 8 && lvl; ++depth) {
				uintptr_t arr = 0, base = 0;
				int ct = 0;
#ifdef _WIN64
				if (!readMem(lvl + 0, arr) || !readMem(lvl + 8, ct)) break;
				readMem(lvl + g_coffs.base, base);
#else
				if (!readMem(lvl + 0, arr) || !readMem(lvl + 4, ct)) break;
				readMem(lvl + 36, base);
#endif
				if (ct <= 0 || ct >= 5000 || !arr) break;
				for (int i = 0; i < ct; ++i) {
					uintptr_t pr = arr + (uintptr_t)i * stride;
					int tp = -1, off = -1;
					uintptr_t nm = 0, v = 0;
					if (!readMem(pr + offType, tp) || tp != 7) continue;
					if (!readMem(pr + offOff, off) || off < 0 || off > 4096) continue;
					if (pass == 0) {
						if (!readMem(pr + offName, nm)) continue;
						char buf[72];
						if (!readText(nm, buf, sizeof buf)) continue;
						if (strcmp(buf, "Weapons") != 0) continue;
					}
					if (!readMem(player + (uintptr_t)off, v) || !v) continue;
					if (pass == 1) {
						char cls[72];
						if (!GetEntityClassName(v, cls, sizeof cls)) continue;
						if (strcmp(cls, "Player Weapons") != 0) continue;
					}
					g_wpnEntity = v;
					g_wpnPec = pec;
					g_wpnOff = off;
					return v;
				}
				lvl = base;
			}
		}
		return 0;
	}

	static int g_ammoCur[6] = { -1, -1, -1, -1, -1, -1 };
	static int g_ammoMax[6] = { -1, -1, -1, -1, -1, -1 };
	static uintptr_t g_ammoPec = 0;
	static bool g_ammoDone = false;

	static void resolveAmmo(uintptr_t weapons)
	{
		for (int i = 0; i < 6; ++i) { g_ammoCur[i] = -1; g_ammoMax[i] = -1; }
		if (!weapons || !resolveClassOffsets(weapons)) return;
#ifdef _WIN64
		const uintptr_t kPecOff = 0x60, stride = 48, offOff = 20, offType = 0;
#else
		const uintptr_t kPecOff = 0x5C, stride = 32, offOff = 12, offType = 0;
#endif
		uintptr_t pec = 0, pdec = 0;
		if (!readMem(weapons + kPecOff, pec) || !pec) return;
		if (!readMem(pec + g_coffs.pec, pdec) || !pdec) return;
		static const int curId[6] = { 40, 42, 44, 46, 50, 52 };
		static const int maxId[6] = { 41, 43, 45, 47, 51, 53 };
		uintptr_t lvl = pdec;
		for (int depth = 0; depth < 8 && lvl; ++depth) {
			uintptr_t arr = 0, base = 0;
			int ct = 0;
#ifdef _WIN64
			if (!readMem(lvl + 0, arr) || !readMem(lvl + 8, ct)) break;
			readMem(lvl + g_coffs.base, base);
#else
			if (!readMem(lvl + 0, arr) || !readMem(lvl + 4, ct)) break;
			readMem(lvl + 36, base);
#endif
			if (ct <= 0 || ct >= 5000 || !arr) break;
			for (int i = 0; i < ct; ++i) {
				uintptr_t pr = arr + (uintptr_t)i * stride;
				int tp = -1, off = -1;
				unsigned id = 0;
				if (!readMem(pr + offType, tp) || tp != 9) continue;
				if (!readMem(pr + offOff, off) || off < 0 || off > 4096) continue;
				if (!readMem(pr + 16, id)) continue;
				id &= 0xFF;
				for (int k = 0; k < 6; ++k) {
					if (g_ammoCur[k] < 0 && (int)id == curId[k]) g_ammoCur[k] = off;
					if (g_ammoMax[k] < 0 && (int)id == maxId[k]) g_ammoMax[k] = off;
				}
			}
			lvl = base;
		}
	}

	static void topUpAmmo(uintptr_t weapons)
	{
		if (!weapons) return;
		uintptr_t pec = 0;
#ifdef _WIN64
		if (!readMem(weapons + 0x60, pec) || !pec) return;
#else
		if (!readMem(weapons + 0x5C, pec) || !pec) return;
#endif
		if (!g_ammoDone || pec != g_ammoPec) {
			resolveAmmo(weapons);
			g_ammoPec = pec;
			g_ammoDone = true;
		}
		for (int i = 0; i < 6; ++i) {
			if (g_ammoCur[i] < 0 || g_ammoMax[i] < 0) continue;
			int maxV = 0;
			if (!readMem(weapons + (uintptr_t)g_ammoMax[i], maxV) || maxV < 1 || maxV > 1000) continue;
			int curV = 0;
			if (!readMem(weapons + (uintptr_t)g_ammoCur[i], curV)) continue;
			if (curV >= 0 && curV < maxV) writeMem(weapons + (uintptr_t)g_ammoCur[i], &maxV, sizeof maxV);
		}
	}

	static int g_rayOff = -1;
	static uintptr_t g_rayPec = 0;

	uintptr_t GetRayHit()
	{
		uintptr_t player = GetLocalPlayer();
		uintptr_t weapons = findWeaponsEntity(player);
		if (!weapons) return 0;
		uintptr_t pec = 0;
		if (!readMem(weapons +
#ifdef _WIN64
			0x60
#else
			0x5C
#endif
			, pec) || !pec) return 0;
		if (pec != g_rayPec || g_rayOff < 0) {
			g_rayOff = -1;
			g_rayPec = pec;
			if (!resolveClassOffsets(weapons)) return 0;
#ifdef _WIN64
			const uintptr_t stride = 48, offOff = 20, offType = 0;
#else
			const uintptr_t stride = 32, offOff = 12, offType = 0;
#endif
			uintptr_t pdec = 0;
			if (!readMem(pec + g_coffs.pec, pdec) || !pdec) return 0;
			uintptr_t lvl = pdec;
			for (int depth = 0; depth < 8 && lvl && g_rayOff < 0; ++depth) {
				uintptr_t arr = 0, base = 0;
				int ct = 0;
#ifdef _WIN64
				if (!readMem(lvl + 0, arr) || !readMem(lvl + 8, ct)) break;
				readMem(lvl + g_coffs.base, base);
#else
				if (!readMem(lvl + 0, arr) || !readMem(lvl + 4, ct)) break;
				readMem(lvl + 36, base);
#endif
				if (ct <= 0 || ct >= 5000 || !arr) break;
				for (int i = 0; i < ct; ++i) {
					uintptr_t pr = arr + (uintptr_t)i * stride;
					int tp = -1, off = -1;
					unsigned id = 0;
					if (!readMem(pr + offType, tp) || tp != 7) continue;
					if (!readMem(pr + offOff, off) || off < 0 || off > 4096) continue;
					if (!readMem(pr + 16, id) || (id & 0xFF) != 30) continue;
					g_rayOff = off;
					break;
				}
				lvl = base;
			}
		}
		if (g_rayOff < 0) return 0;
		uintptr_t hit = 0;
		readMem(weapons + (uintptr_t)g_rayOff, hit);
		return hit;
	}

	void RapidFireTick()
	{
		if (!rapid) return;
#ifdef _WIN64
		const uintptr_t kTimerOff = 0x128, kNodeOff = 0x118, kIdOff = 0x20;
#else
		const uintptr_t kTimerOff = 0xD8, kNodeOff = 0xD0, kIdOff = 0x1C;
#endif
		uintptr_t player = GetLocalPlayer();
		uintptr_t weapons = findWeaponsEntity(player);
		if (!weapons) return;
		float t = -1.0f;
		if (!readMem(weapons + kTimerOff, t)) return;
		if (t > 10.0f && t < 1000000.0f) {
			float z = 0.0f;
			writeMem(weapons + kTimerOff, &z, sizeof z);
			uintptr_t world = GetWorld();
			uintptr_t node = weapons + kNodeOff;
			for (int s = 0; s < 64; ++s) {
				uintptr_t pred = 0;
#ifdef _WIN64
				if (!readMem(node + 8, pred) || !pred) break;
#else
				if (!readMem(node + 4, pred) || !pred) break;
#endif
				if (world && pred >= world && pred < world + 0x8000) break;
				if (pred < kNodeOff) break;
				uintptr_t ent = pred - kNodeOff;
				int id = -1;
				if (!readMem(ent + kIdOff, id) || id <= 0 || id >= 1000000) break;
				float tt = 0.0f;
				if (!readMem(ent + kTimerOff, tt) || tt <= 0.0f || tt > t) break;
				float z2 = 0.0f;
				writeMem(ent + kTimerOff, &z2, sizeof z2);
				node = pred;
			}
		}
		topUpAmmo(weapons);
	}
}