#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/ItemDefinitionData.h"

#include "Serializers/ObjectSerializer.h"

namespace {
	const TMap<FString, FString> Stands = {
		{ TEXT("Icon"),      TEXT("SmallPreviewImage") },
		{ TEXT("LargeIcon"), TEXT("LargePreviewImage") },
		{ TEXT("Tags"),      TEXT("GameplayTags")      },
		{ TEXT("Traits"),    TEXT("Traits")            },
		{ TEXT("Series"),    TEXT("Series")            },
		{ TEXT("Rarity"),    TEXT("Rarity")            },
		{ TEXT("MinLevel"),  TEXT("MinLevel")          },
		{ TEXT("MaxLevel"),  TEXT("MaxLevel")          },
	};
}

int32 FItemDefinitionData::Apply(UObject* Asset, const TSharedPtr<FJsonObject>& Data, const UObjectSerializer* Serializer) {
	if (Asset == nullptr || !Data.IsValid() || Serializer == nullptr) return 0;

	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;

	if (!Data->TryGetArrayField(TEXT("DataList"), List)) return 0;

	const TSharedPtr<FJsonObject> Said = MakeShared<FJsonObject>();

	int32 Wrote = 0;

	for (const TSharedPtr<FJsonValue>& Value : *List) {
		if (!Value.IsValid() || Value->Type != EJson::Object) continue;

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Carried : Value->AsObject()->Values) {
			const FString* Called = Stands.Find(Carried.Key);

			if (Called == nullptr) continue;

			if (Data->HasField(*Called)) continue;

			if (FindFProperty<FProperty>(Asset->GetClass(), **Called) == nullptr) continue;

			Said->SetField(*Called, Carried.Value);

			Wrote++;
		}
	}

	if (Wrote > 0) {
		Serializer->DeserializeObjectProperties(Said, Asset);
	}

	return Wrote;
}
