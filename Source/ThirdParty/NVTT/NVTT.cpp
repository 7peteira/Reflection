#include "Modules/ModuleManager.h"
#include "NVTT.h"

void FNVTTModule::StartupModule() {
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FNVTTModule::ShutdownModule() {
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

IMPLEMENT_MODULE(FNVTTModule, ReflectionNVTT)

// Vendored NVTT image/DDS implementation (UE4 UBT does not compile nested ThirdParty sources).
#include "ThirdParty/nvtt/nvimage/BlockDXT.cpp"
#include "ThirdParty/nvtt/nvimage/ColorBlock.cpp"
#include "ThirdParty/nvtt/nvimage/DirectDrawSurface.cpp"
#include "ThirdParty/nvtt/nvimage/Image.cpp"
