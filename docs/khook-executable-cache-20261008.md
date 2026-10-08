# API18 virtual trampolines and executable-map cache — 08.10.2026

## Confirmed failure

On WW3 server №51 (panel id89), CS2 PID742832, Utils rejected all four observed pawn Teleport virtual targets before dispatch. Every target was inside a current executable anonymous mapping, but outside Utils' initial executable-range cache (191 ranges, initialized). `PlayersApi::Teleport` invokes `vmt::IsExecutableAddress` immediately before calling the target; its failure branch skips the engine call. WW3's unconditional `moved` counter therefore measured attempted calls, not successful movement.

The exact loaded game ELF SHA256 was `acfb37875451ce309028313ea79f3687eee9a87dbde54bfda79bf1e5384ba329`. Private proof is retained separately in `work/api18-07-51/ww3-teleport-executable-cache-proof-1791440946271538354.json` and its scoped native investigation report. No credentials or player identifiers are included here.

## Native and schema checks

Teleport slot164 is valid. Its original pawn wrapper RVA0x15da610 forwards all three pointer arguments before postprocessing, through0x161a810,0xd6ae00 and0xd6a440. The position setter uses0xd40c30 then0x1728a60 and writes origin synchronously. The separate pawn slot87 returnsfalse and does not cancel this setter. These addresses are investigation evidence for this exact SHA, never fallback offsets in production code.

Schema offsets came from the SHA-matching engine-watch baseline snapshot `25687242.GUt9Pi/schema.json`: `CBaseEntity.m_CBodyComponent=48`, `CBodyComponent.m_pSceneNode=8`, `CGameSceneNode.m_vecAbsOrigin=200`. For all four checked pawns the resulting scene pointer matched the native pointer at pawn+0x528. Neither the teleport slot nor scene lookup needed a changed gamedata value.

The engine executable PT_LOAD has a virtual-address/file-offset difference of0x1000. Initial disassembly using the uncorrected file-offset-based RVA0x15d9610 did not describe the original function and was discarded.

## Pinned KHook contract

Reviewed primary source: MetaMod `5e23f84fec37b99c6f8f7b6546dd83a8d0f71078`, KHook submodule `2a8953533da2dd473191f35448e21fcbe5667cc2`, `third_party/khook/src/detour.cpp`, `FindOriginalVirtual` (line1883).

The implementation takes the hook registry's shared lock and looks up the virtual-slot address `table+index`. A registered virtual hook returns its stored original function. An unregistered slot returns its current `table[index]`. Therefore a different original validates a known registry entry; an unregistered target fails the proposed cache-miss check because original equals target. The API does not expose a separate registered-trampoline address; registration ownership and an unchanged current slot are the evidence available through this public interface.

`Virtual::_KHook_MakeOriginalCall` in the same pinned `include/khook.hpp` builds the method pointer from `GetOriginalFunction`, forwards all template arguments, and saves the result. The Ignore action leaves original dispatch intact.

## Reviewed repair

`IsCallableVirtual(table,index,target)` keeps the existing cached executable-address fast path. On a cache miss, an API18 consumer consults `KHook::__exported__khook->FindOriginalVirtual` only when the interface exists. It accepts a different non-null cached-executable original while the current slot still equals the captured target. Null targets, unavailable KHook, unregistered cache-miss targets, unvalidated originals and changed slots are rejected.

`GetVMethod` returns the current target after that check. It never substitutes or directly invokes the original, so CS2AC and other hook callbacks remain in the call chain. `ResolveSemanticSlotInTable` uses the same callable check. The repair adds no maps refresh, filesystem I/O, signature scan, retry, or delivery API gate in the hot path. The existing first-use maps snapshot remains unchanged.

The KHook include and registry path are guarded by `defined(METAMOD_PLAPI_VERSION) && METAMOD_PLAPI_VERSION>=18`. Non-MetaMod/API17 consumers retain the existing cached-only behavior and do not acquire a new KHook header dependency. Reviewed production include ordering in Utils and WW3 defines the MetaMod API before the shared header. Actual pinned-header regression and build results are recorded by the release task; this review does not claim runtime verification of a not-yet-delivered binary.

## Provenance

Pre-repair source: SchemaEntity `2e5cf5acb0b5a826d9c2de7bc6456b3c2baaac91`, `virtual.h`; preserved exact original and origin JSON in the private evidence directory before release. Investigation was read-only (`/proc/PID/maps`, bounded `/proc/PID/mem`, disk ELF disassembly, existing SHA-bound baseline and pinned KHook source). It did not invoke functions in the process, attach ptrace, disable CS2AC/FOW, or change game files. No commit or deployment was performed by the independent review agent.
