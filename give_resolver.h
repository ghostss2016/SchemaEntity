#pragma once
// ЦЕНТРАЛЬНЫЙ резолвер выдачи предметов — единая цепочка для всех VIP/give-плагинов (CS2 build 2000873).
//
// ПОЧЕМУ: GiveNamedItem захукана инлайн-детуром (первый байт пролога = 0xE9 jmp, доказано дампом
// байт в рантайме: `e9 9b 14 11 ec ...` вместо `55 48 89 e5 ...`). Поэтому сигнатура ПРОЛОГА не
// матчится, а vtable-подсчёт слотов хрупок. Надёжное решение:
//   1) EquipWeapon НЕ хукают → её сигнатуру находим через /proc/self/maps (dl_iterate_phdr не видит
//      libserver у Source2-загрузчика);
//   2) от найденного адреса EquipWeapon вычисляем базу загрузки libserver;
//   3) GiveNamedItem = base + VMA (вызов идёт через хук-трамплин — корректно, как это делает движок).
//
// НА ОБНОВЛЕНИИ ДВИЖКА: обновить VMA_* и SIG_* из gamedata (sig_gen.py по свежему декомпилю) — и ВСЕ
// плагины чинятся по цепочке одним изменением этого файла.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

class CBasePlayerWeapon;

namespace CS2Give
{
	typedef CBasePlayerWeapon* (*GiveNamedItem_t)(void* itemServices, const char* name, int subType, void* scriptItem, bool removeIfNotCarried, void* unknown);
	typedef void (*EquipWeapon_t)(void* weaponServices, CBasePlayerWeapon* weapon);

	// ---- Сигнатуры прологов (основной путь — авто-находят адрес на ЛЮБОМ билде, пока пролог цел). ----
	static const char*     SIG_GiveNamedItem = "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 81 EC ? ? ? ? 48 89 BD ? ? ? ? 89 95 ? ? ? ? 48 89 8D ? ? ? ? 44 89 85";
	static const char*     SIG_EquipWeapon   = "55 48 89 E5 41 55 41 54 49 89 FC 53 48 89 F3 48 83 EC ? 48 8B 77";
	// Тот же пролог БЕЗ первых 5 байт: ровно столько затирает инлайн-детур (`e9` + смещение).
	// Нужен, чтобы находить функцию, даже когда её уже захукали, — без единой константы адреса.
	// Проверено на libserver.so от 04.08.2026: хвост уникален (1 совпадение).
	static const char*     SIG_GiveNamedItem_Tail = "41 57 41 56 41 55 41 54 53 48 81 EC ? ? ? ? 48 89 BD ? ? ? ? 89 95 ? ? ? ? 48 89 8D ? ? ? ? 44 89 85";
	static const size_t    SIG_GiveNamedItem_TailSkip = 5;

	// ⛔ ЗАШИТЫХ АДРЕСОВ ЗДЕСЬ БОЛЬШЕ НЕТ — И ДОБАВЛЯТЬ ИХ НЕЛЬЗЯ.
	// Раньше тут лежали VMA_GiveNamedItem/VMA_EquipWeapon, и база движка считалась как
	// «найденный адрес − VMA». Из-за этого переезд одной функции уводил базу, а вместе с ней
	// и ВСЕ адреса, посчитанные от неё. Обновление 04.08.2026 сдвинуло EquipWeapon на 0x48c0 —
	// выдача предмета ушла в середину чужой функции, и серверы падали на спавне пачками.
	// Теперь адрес каждой функции ищется сигнатурой на живом процессе, а не арифметикой от
	// константы. Не находится — возвращаем nullptr: лучше не выдать предмет, чем звать мусор.

	// Скан паттерна ("55 48 ? 89", ? = wildcard) по r-x регионам modules из /proc/self/maps.
	inline uintptr_t ProcMapsScan(const char* moduleSubstr, const char* pattern, int* pCount = nullptr)
	{
		if (pCount) *pCount = 0;
		unsigned char bytes[512]; char mask[512]; size_t len = 0;
		for (const char* c = pattern; *c && len < 512; ) {
			if (*c == ' ') { c++; continue; }
			if (*c == '?') { mask[len] = '?'; bytes[len] = 0; len++; c++; if (*c == '?') c++; }
			else { bytes[len] = (unsigned char)strtol(c, nullptr, 16); mask[len] = 'x'; len++; c += 2; }
		}
		if (!len) return 0;
		FILE* f = fopen("/proc/self/maps", "r");
		if (!f) return 0;
		char line[512]; uintptr_t first = 0; int cnt = 0;
		while (fgets(line, sizeof(line), f)) {
			if (!strstr(line, moduleSubstr)) continue;
			char* sp = strchr(line, ' ');
			if (!sp) continue;
			char* perms = sp + 1;
			if (!(perms[0] == 'r' && perms[2] == 'x')) continue; // только r-x
			unsigned long a = 0, b = 0;
			if (sscanf(line, "%lx-%lx", &a, &b) != 2 || b <= a) continue;
			unsigned char* base = reinterpret_cast<unsigned char*>((uintptr_t)a);
			size_t size = (size_t)(b - a);
			for (size_t o = 0; o + len <= size; o++) {
				bool m = true;
				for (size_t j = 0; j < len; j++) if (mask[j] == 'x' && base[o + j] != bytes[j]) { m = false; break; }
				if (m) { if (!first) first = reinterpret_cast<uintptr_t>(base + o); cnt++; }
			}
		}
		fclose(f);
		if (pCount) *pCount = cnt;
		return first;
	}

	// Базовый адрес загрузки libserver.so — берётся НАПРЯМУЮ из карты памяти процесса.
	// Никакого вычитания константы: самый низкий адрес среди r-x областей модуля и есть база.
	// Оставлено для тех, кому база нужна сама по себе; резолв функций её больше не использует.
	inline uintptr_t ModuleBase()
	{
		static uintptr_t s_base = (uintptr_t)-1;
		if (s_base != (uintptr_t)-1) return s_base;
		s_base = 0;
		FILE* f = fopen("/proc/self/maps", "r");
		if (!f) return s_base;
		char line[512];
		uintptr_t lowest = 0;
		while (fgets(line, sizeof(line), f)) {
			if (!strstr(line, "libserver.so")) continue;
			unsigned long a = 0, b = 0;
			if (sscanf(line, "%lx-%lx", &a, &b) != 2) continue;
			if (!lowest || (uintptr_t)a < lowest) lowest = (uintptr_t)a;
		}
		fclose(f);
		s_base = lowest;
		return s_base;
	}

	// Адрес GiveNamedItem на ЖИВОМ процессе. Два пути, оба — поиск по байтам, без констант:
	//   1) целый пролог — когда функция не захукана;
	//   2) хвост пролога без первых 5 байт — когда её затёр инлайн-детур; отступаем назад
	//      на эти 5 байт и получаем настоящее начало функции.
	// Ни один не сошёлся → nullptr. Звать мусор нельзя: именно это роняло серверы 04.08.2026.
	inline GiveNamedItem_t GiveNamedItem()
	{
		static GiveNamedItem_t s_fn = nullptr;
		static bool s_done = false;
		if (s_done) return s_fn;
		s_done = true;
		int c = 0;
		uintptr_t a = ProcMapsScan("libserver.so", SIG_GiveNamedItem, &c);
		if (c == 1 && a) { s_fn = reinterpret_cast<GiveNamedItem_t>(a); return s_fn; }
		c = 0;
		a = ProcMapsScan("libserver.so", SIG_GiveNamedItem_Tail, &c);
		if (c == 1 && a) { s_fn = reinterpret_cast<GiveNamedItem_t>(a - SIG_GiveNamedItem_TailSkip); }
		return s_fn;
	}

	// Адрес EquipWeapon — так же поиском по прологу. Её не хукают, одного пути достаточно.
	inline EquipWeapon_t EquipWeapon()
	{
		static EquipWeapon_t s_fn = nullptr;
		static bool s_done = false;
		if (s_done) return s_fn;
		s_done = true;
		int c = 0;
		uintptr_t a = ProcMapsScan("libserver.so", SIG_EquipWeapon, &c);
		if (c == 1 && a) s_fn = reinterpret_cast<EquipWeapon_t>(a);
		return s_fn;
	}
}
