#include "Modules/ModuleManager.h"
#include "Detex.h"

void FDetexModule::StartupModule() {
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FDetexModule::ShutdownModule() {
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

IMPLEMENT_MODULE(FDetexModule, ReflectionDetex)

// Vendored Detex implementation (kept in this module so UE4 UBT links its API).
#include "ThirdParty/detex/bits.cpp"
#include "ThirdParty/detex/bptc-tables.cpp"
#include "ThirdParty/detex/clamp.cpp"
#include "ThirdParty/detex/convert.cpp"
#include "ThirdParty/detex/dds.cpp"
#include "ThirdParty/detex/decompress-bc.cpp"
#include "ThirdParty/detex/decompress-bptc-float.cpp"
#include "ThirdParty/detex/decompress-bptc.cpp"
#include "ThirdParty/detex/decompress-eac.cpp"
#include "ThirdParty/detex/decompress-etc.cpp"
#include "ThirdParty/detex/division-tables.cpp"
#include "ThirdParty/detex/file-info.cpp"
#include "ThirdParty/detex/half-float.cpp"
#include "ThirdParty/detex/misc.cpp"
#include "ThirdParty/detex/texture.cpp"
