// ---------------------------------------------------------------------------
// Platform-level TechnoClass extension lifecycle.
//
// One extension instance per techno (see src/Ext/TechnoDataExt.h and
// docs/EngineBindings.md "Extension model").  The instance is created for every
// techno when TechnoClass::Init returns and removed by the TechnoClass base
// destructor, so the extensions of all per-techno platform features share the
// same lifetime:
//
//   0x6F42E3  `mov dword [esi+0x49c],1` (10 bytes), near the end of
//             TechnoClass::Init; all four concrete techno classes funnel through
//             it and ESI holds the techno.  The window is the whole store and
//             the next instruction starts at 0x6F42ED: SyringeEx replays the
//             bytes the window covers after the handler returns 0, so the size
//             has to span complete instructions - a window that cuts this
//             10-byte store in half replays truncated bytes and crashes the
//             game on the first techno.  A free site: none of the five
//             audited ecosystem hook tables uses it.  (The official Phobos
//             alpha2 hooks 0x6F42F7, the `pop esi` + `ret` tail, with a 2-byte
//             window - the platform's verifier requires at least the 5-byte near
//             jump, so the instruction before it is the site.  The only path
//             that skips this site is the early exit for an object whose
//             GetTechnoType() is null, which is not a usable techno.)
//   0x6F4500  `push ecx; push ebx; push esi; mov esi,ecx`, the TechnoClass
//             base destructor entry (all four concrete destructors chain into
//             it).  ECX holds the techno.  This is one of the framework
//             addresses five ecosystem platforms share (Ares / Kratos / Mix /
//             Phobos lineage and one closed-source host removed from the public
//             surface); the audited handlers at this address are the "return 0,
//             keep the chain going" kind and no platform takes the window over.
//
// A savegame round trip: the extension carries no serialized state, so while a
// savegame loads the store defers the allocation and the platform's post-load
// step creates the instance for the objects the savegame brought back.  Nothing
// is written to the savegame and no version is bumped.
// ---------------------------------------------------------------------------

#include "Ext/TechnoDataExt.h"

#include "Core/Log.h"
#include "Engine/EngineCompat.h"

#include "Ext/ExtensionIndex.h"

namespace venture
{
namespace
{
// Registers the store (diagnostic name + "no savegame rows") with the extension
// index before any hook can fire: the DLL's static initializers complete before
// SyringeEx installs the hooks, so the first TechnoClass::Init already sees a
// named store.
struct TechnoDataRegistrar
{
	TechnoDataRegistrar()
	{
		TechnoData().SetName("TechnoClass/TechnoDataExt");
		TechnoData().SetSaveable(false);
	}
};

const TechnoDataRegistrar g_technoDataRegistrar;

unsigned PtrOf(const void* const pointer)
{
	return static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(pointer));
}
} // namespace

TechnoDataStore& TechnoData()
{
	return TechnoDataStore::Instance();
}

void AllocateTechnoData(TechnoClass* const pTechno)
{
	if (!pTechno)
		return;

	if (TechnoData().Fetch(pTechno) != nullptr)
		return;

	TechnoDataExt* const pExt = TechnoData().Allocate(pTechno);

	if (pExt != nullptr)
	{
		Log::Debug("[TechnoData] extension allocated for TechnoClass 0x%08X", PtrOf(pTechno));
		return;
	}

	// While a savegame loads the allocation is deferred on purpose: the trailer
	// may restore this techno's extension and the post-load step fills in the
	// rest.  Not an error.
	if (ExtensionIndex::Instance().IsLoadingSaveGame())
		return;

	// Another store already claimed this owner: a feature-composition error, see
	// ExtensionIndex::Attach.
	Log::Warn("[TechnoData] TechnoClass 0x%08X already has an extension of another store",
		PtrOf(pTechno));
}

void RemoveTechnoData(TechnoClass* const pTechno)
{
	if (!pTechno || TechnoData().Fetch(pTechno) == nullptr)
		return;

	TechnoData().Remove(pTechno);
}
} // namespace venture

// --- lifecycle hooks -------------------------------------------------------
// TechnoClass::Init, near the tail: allocate the platform's per-techno
// extension.  Window = the full 10-byte `mov dword [esi+0x49c],1`; the next
// instruction is at 0x6F42ED.
DEFINE_HOOK(0x6F42E3, Venture_TechnoData_Init, 0xA)
{
	GET(TechnoClass*, pThis, ESI);
	venture::AllocateTechnoData(pThis);
	return 0;
}

// TechnoClass base destructor: drop the extension before the object goes away.
DEFINE_HOOK(0x6F4500, Venture_TechnoData_DTOR, 0x5)
{
	GET(TechnoClass*, pThis, ECX);
	venture::RemoveTechnoData(pThis);
	return 0;
}
