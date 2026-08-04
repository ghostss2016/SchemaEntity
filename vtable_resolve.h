#pragma once
/**
 * Самонаводящийся номер слота виртуальной функции.
 *
 * ЗАЧЕМ. `CALL_VIRTUAL(void, 24, this)` — это буквально `vtable[24]`. В таблице
 * виртуальных функций нет имён, поэтому число нельзя «вычислить»; а обновление игры
 * вставляет новую виртуальную функцию выше по таблице, и все номера ниже съезжают.
 * Вызов молча уходит в соседнюю функцию и роняет сервер в чужом коде — 04.08.2026
 * слот 380 вместо 384 указывал в середину чужой команды.
 *
 * ИДЕЯ. Число перестаёт быть истиной и становится подсказкой, ГДЕ ИСКАТЬ. Истина —
 * байты самой функции. Пока всё исправно, плагин запоминает отпечаток того, что лежит
 * в слоте. После обновы он сверяет: тот же код в ячейке — номер верен; не тот —
 * ищет отпечаток по соседним слотам и продолжает работать с найденным номером.
 *
 * Почему отпечаток снимается на живом объекте, а не в бинаре: начало таблицы в
 * стрипнутом libserver.so надёжно не определяется (обход упирается в заголовок
 * соседней таблицы, а сохранённые базы устаревают с каждой обновой). В рантайме
 * начало даётся даром — его приносит сам объект: `*(void***)pObject`.
 *
 * ЧЕГО НЕ ДЕЛАЕТ. Если Valve перепишет тело функции, отпечаток перестанет совпадать.
 * Тогда возможность отключается и пишется в лог — сервер живёт, а причина видна
 * сразу, а не восстанавливается по дампу.
 */

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>

#include "virtual.h"

namespace vmt
{
	// Сколько байт функции берём как отпечаток. Пролог короче путать нельзя: слишком
	// короткий совпадёт с чужой функцией, слишком длинный сломается от любой пересборки.
	static const size_t kPrintLen = 24;
	// Насколько далеко ищем съехавший слот. Обнова двигает номера на единицы;
	// широкий поиск лишь повышает шанс наткнуться на похожую чужую функцию.
	static const int    kScanRadius = 48;

	inline std::string FingerprintPath()
	{
		// Рядом с остальными конфигами сервера: файл общий для всех плагинов,
		// потому что таблицы у них одни и те же.
		return "addons/configs/vtable_fingerprints.txt";
	}

	inline std::unordered_map<std::string, std::string> &FingerprintStore()
	{
		static std::unordered_map<std::string, std::string> s_map;
		static bool s_loaded = false;
		if (!s_loaded)
		{
			s_loaded = true;
			if (FILE *f = fopen(FingerprintPath().c_str(), "r"))
			{
				char line[512];
				while (fgets(line, sizeof(line), f))
				{
					char *eq = strchr(line, '=');
					if (!eq) continue;
					*eq = '\0';
					std::string k(line), v(eq + 1);
					while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
					while (!k.empty() && k.back() == ' ') k.pop_back();
					if (!k.empty() && !v.empty()) s_map[k] = v;
				}
				fclose(f);
			}
		}
		return s_map;
	}

	inline std::string BytesToHex(const unsigned char *p, size_t n)
	{
		static const char *H = "0123456789ABCDEF";
		std::string s;
		s.reserve(n * 3);
		for (size_t i = 0; i < n; i++)
		{
			if (i) s += ' ';
			s += H[p[i] >> 4];
			s += H[p[i] & 0xF];
		}
		return s;
	}

	/** Раскрыть переходник: в ячейке может лежать `jmp настоящая_функция`. */
	inline const unsigned char *FollowThunk(const unsigned char *p)
	{
		if (!p || !IsExecutableAddress(p)) return p;
		if (p[0] == 0xE9)                                   // jmp rel32
		{
			int32_t rel;
			memcpy(&rel, p + 1, sizeof(rel));
			const unsigned char *t = p + 5 + rel;
			return IsExecutableAddress(t) ? t : p;
		}
		return p;
	}

	inline bool MatchesPrint(void *pTarget, const std::string &print)
	{
		if (!pTarget || !IsExecutableAddress(pTarget)) return false;
		const unsigned char *code = FollowThunk(static_cast<const unsigned char *>(pTarget));
		return BytesToHex(code, kPrintLen) == print;
	}

	/**
	 * Вернуть рабочий номер слота для объекта.
	 *
	 * key   — устойчивое имя («CCSPlayer_ItemServices::StripPlayerWeapons»);
	 * hint  — номер, который был верен на момент написания кода;
	 * Возврат -1 означает «не нашли» — вызывать НЕЛЬЗЯ, возможность надо отключить.
	 */
	inline int ResolveSlot(void *pObject, uint32 hint, const char *key)
	{
		static std::unordered_map<std::string, int> s_cache;
		const std::string k(key);

		auto it = s_cache.find(k);
		if (it != s_cache.end()) return it->second;

		if (!pObject) return (int)hint;                     // нечего проверять — доверяем подсказке
		void **vt = *static_cast<void ***>(pObject);
		if (!vt) return (int)hint;

		auto &store = FingerprintStore();
		auto fit = store.find(k);

		// Отпечатка ещё нет: система исправна — запоминаем, что лежит в подсказанном слоте.
		if (fit == store.end())
		{
			void *pT = vt[hint];
			if (!pT || !IsExecutableAddress(pT))
			{
				Warning("[vtable] %s: слот %u не указывает на код, а отпечатка ещё нет — "
						"возможность отключена\n", key, hint);
				s_cache[k] = -1;
				return -1;
			}
			const unsigned char *code = FollowThunk(static_cast<const unsigned char *>(pT));
			std::string print = BytesToHex(code, kPrintLen);
			store[k] = print;
			if (FILE *f = fopen(FingerprintPath().c_str(), "a"))
			{
				fprintf(f, "%s=%s\n", key, print.c_str());
				fclose(f);
			}
			s_cache[k] = (int)hint;
			return (int)hint;
		}

		// Отпечаток есть: сперва проверяем подсказанный слот — обычный случай.
		if (MatchesPrint(vt[hint], fit->second))
		{
			s_cache[k] = (int)hint;
			return (int)hint;
		}

		// Слот съехал. Ищем отпечаток по соседям — от ближних к дальним.
		for (int d = 1; d <= kScanRadius; d++)
		{
			for (int sign = -1; sign <= 1; sign += 2)
			{
				const int idx = (int)hint + sign * d;
				if (idx < 0) continue;
				if (MatchesPrint(vt[idx], fit->second))
				{
					Warning("[vtable] %s: слот съехал %u → %d (обновление движка). "
							"Работаем по найденному.\n", key, hint, idx);
					s_cache[k] = idx;
					return idx;
				}
			}
		}

		Warning("[vtable] %s: функция не найдена ни в одном слоте рядом с %u — "
				"возможность отключена. Похоже, движок переписал её тело.\n", key, hint);
		s_cache[k] = -1;
		return -1;
	}

	/** Вызов с самонаведением. Ничего не делает, если слот не сошёлся. */
	template <typename T, typename... Args>
	inline T CallVirtualResolved(const char *key, uint32 hint, void *pClass, Args... args)
	{
		const int idx = ResolveSlot(pClass, hint, key);
		if (idx < 0) return T();
		return CallVirtual<T>((uint32)idx, pClass, args...);
	}
}

/**
 * Замена CALL_VIRTUAL там, где номер слота задан числом.
 * Старый макрос намеренно оставлен нетронутым: его тянут десятки плагинов.
 */
#define CALL_VIRTUAL_RESOLVED(retType, key, hint, ...) \
	vmt::CallVirtualResolved<retType>(key, hint, __VA_ARGS__)
