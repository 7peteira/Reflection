#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Cascade/ParticleSystemImporter.h"

#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleSystem.h"

UObject* IParticleSystemImporter::CreateAsset(UObject* CreatedAsset) {
	return IImporter::CreateAsset(NewObject<UParticleSystem>(GetPackage(), UParticleSystem::StaticClass(), *GetAssetName(), RF_Public | RF_Standalone));
}

bool IParticleSystemImporter::Import() {
	const auto ParticleSystem = Create<UParticleSystem>();

	WipeEmitters();

	GetObjectSerializer()->bUseExperimentalSpawning = true;
	GetObjectSerializer()->DeserializeObjectProperties(GetAssetData(), ParticleSystem);

	return OnAssetCreation(ParticleSystem);
}

void IParticleSystemImporter::WipeEmitters() const {
	const auto ParticleSystem = GetTypedAsset<UParticleSystem>();

	if (ParticleSystem->Emitters.Num() > 0) {
		for (UParticleEmitter* Emitter : ParticleSystem->Emitters) {
			if (Emitter) {
				Emitter->ConditionalBeginDestroy();
			}
		}

		ParticleSystem->Emitters.Empty();
	}
}
