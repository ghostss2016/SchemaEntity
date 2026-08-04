/**
 * =============================================================================
 * CS2Fixes
 * Copyright (C) 2023 Source2ZE
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#include "platform.h"

#include <cstdio>
#include <cstring>
#include <vector>
#include <utility>

#define CALL_VIRTUAL(retType, idx, ...) \
	vmt::CallVirtual<retType>(idx, __VA_ARGS__)

namespace vmt
{
	/**
	 * Лежит ли адрес в исполняемой памяти процесса.
	 *
	 * Зачем. Номер слота — обычный индекс массива, и в таблице виртуальных функций
	 * нет имён: что лежит в ячейке, проверить нечем. Стоит движку вставить новую
	 * виртуальную функцию выше по таблице — все номера ниже съезжают, и вызов молча
	 * уходит в соседнюю функцию. Так 04.08.2026 серверы падали пачками: попадали в
	 * трёхкомандный геттер, который разыменовывал мусор.
	 *
	 * Полностью это лечится только поиском функции по байтам, но такая проверка
	 * превращает тихое падение в громкий отказ: адрес обязан быть кодом, а не
	 * данными и не мусором. Карту читаем один раз — после загрузки она не меняется,
	 * а вызовы идут каждый кадр.
	 */
	inline bool IsExecutableAddress(const void *p)
	{
		static std::vector<std::pair<uintptr_t, uintptr_t>> s_ranges;
		static bool s_loaded = false;
		if (!s_loaded)
		{
			s_loaded = true;
			if (FILE *f = fopen("/proc/self/maps", "r"))
			{
				char line[512];
				while (fgets(line, sizeof(line), f))
				{
					unsigned long a = 0, b = 0;
					char perms[8] = {0};
					if (sscanf(line, "%lx-%lx %7s", &a, &b, perms) != 3) continue;
					if (perms[2] != 'x') continue;
					s_ranges.emplace_back((uintptr_t)a, (uintptr_t)b);
				}
				fclose(f);
			}
		}
		if (s_ranges.empty()) return true;        // карту прочитать не удалось — не мешаем работать
		const uintptr_t v = reinterpret_cast<uintptr_t>(p);
		for (const auto &r : s_ranges)
			if (v >= r.first && v < r.second) return true;
		return false;
	}

	template <typename T = void *>
	inline T GetVMethod(uint32 uIndex, void *pClass)
	{
		if (!pClass)
		{
			Warning("Tried getting virtual function from a null class.\n");
			return T();
		}

		void **pVTable = *static_cast<void ***>(pClass);
		if (!pVTable)
		{
			Warning("Tried getting virtual function from a null vtable.\n");
			return T();
		}

		void *pTarget = pVTable[uIndex];
		if (!pTarget || !IsExecutableAddress(pTarget))
		{
			// Номер слота устарел после обновления движка. Молчать нельзя: без этого
			// сообщения остаётся только дамп с адресом посреди чужой функции.
			Warning("Virtual slot %u points outside executable memory (%p) — call skipped. "
					"Slot indices are stale after a CS2 update.\n", uIndex, pTarget);
			return T();
		}

		return reinterpret_cast<T>(pTarget);
	}

	template <typename T, typename... Args>
	inline T CallVirtual(uint32 uIndex, void *pClass, Args... args)
	{
#ifdef _WIN32
		auto pFunc = GetVMethod<T(__thiscall *)(void *, Args...)>(uIndex, pClass);
#else
		auto pFunc = GetVMethod<T(__cdecl*)(void*, Args...)>(uIndex, pClass);
#endif
		if (!pFunc)
		{
			Warning("Tried calling a null virtual function.\n");
			return T();
		}

		return pFunc(pClass, args...);
	}
}