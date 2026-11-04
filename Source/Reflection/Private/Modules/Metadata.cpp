#include "Engine/SharedPtrCompatibility.h"
#include "Modules/Metadata.h"

#include "Interfaces/IPluginManager.h"
#include "Engine/EngineUtilities.h"
#include "Engine/Log.h"

FName GReflectionName = FName("Reflection");

TSharedPtr<IPlugin> FRMetadata::Plugin = nullptr;
FString FRMetadata::Version = "";

void FRMetadata::Initialize() {
    Plugin = GetPlugin(GReflectionName.ToString());

    if (!Plugin.IsValid()) {
        UE_LOG(LogReflection, Warning, TEXT("No plugin named \"%s\" is loaded, so its version is unknown"), *GReflectionName.ToString());

        Version = FString();

        return;
    }

    Version = Plugin->GetDescriptor().VersionName;
}
