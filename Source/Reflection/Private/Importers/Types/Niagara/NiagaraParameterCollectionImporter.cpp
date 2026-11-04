#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Niagara/NiagaraParameterCollectionImporter.h"

#include "Engine/EngineUtilities.h"

#include "NiagaraParameterCollection.h"

namespace {
#if UE4_23_BELOW
	struct FStoredParameter {
		FNiagaraVariable Variable;
		int32 Offset;

		FName GetName() const { return Variable.GetName(); }
		int32 GetSizeInBytes() const { return Variable.GetSizeInBytes(); }
	};

	TArray<FStoredParameter> ReadStoredParameters(const FNiagaraParameterStore& Store) {
		TArray<FStoredParameter> Read;

		Read.Reserve(Store.GetParameterOffsets().Num());

		for (const TPair<FNiagaraVariable, int32>& Parameter : Store.GetParameterOffsets()) {
			Read.Add(FStoredParameter{ Parameter.Key, Parameter.Value });
		}

		return Read;
	}
#elif UE4_25_BELOW
	const TArray<FNiagaraVariableWithOffset>& ReadStoredParameters(const FNiagaraParameterStore& Store) {
		return Store.GetSortedParameterOffsets();
	}
#else
	TArrayView<const FNiagaraVariableWithOffset> ReadStoredParameters(const FNiagaraParameterStore& Store) {
		return Store.ReadParameterVariables();
	}
#endif
}

void INiagaraParameterCollectionImporter::Repair(UObject* Asset) const {
	UNiagaraParameterCollection* Collection = Cast<UNiagaraParameterCollection>(Asset);

	if (Collection == nullptr) return;

	UNiagaraParameterCollectionInstance* Instance = Collection->GetDefaultInstance();

	if (Instance == nullptr) return;

	FNiagaraParameterStore& Store = Instance->GetParameterStore();

	const TArray<uint8> Arrived = Store.GetParameterDataArray();

	TMap<FName, TArray<uint8>> Overrides;

	for (const auto& Variable : ReadStoredParameters(Store)) {
		const int32 Size = Variable.GetSizeInBytes();

		if (Variable.Offset < 0 || Size <= 0 || Variable.Offset + Size > Arrived.Num()) continue;

		Overrides.Add(Variable.GetName(), TArray<uint8>(Arrived.GetData() + Variable.Offset, Size));
	}

	Store.Empty();

	int32 Kept = 0;

	for (const FNiagaraVariable& Parameter : Collection->GetParameters()) {
		if (const TArray<uint8>* Bytes = Overrides.Find(Parameter.GetName())) {
			Store.SetParameterData(Bytes->GetData(), Parameter, true);

			Kept++;
		} else if (Parameter.IsDataAllocated()) {
			Store.SetParameterData(Parameter.GetData(), Parameter, true);
		} else {
			Store.AddParameter(Parameter, true, false);
		}
	}

#if !UE4_23_BELOW
	Store.SortParameters();
#endif

	UE_LOG(LogReflection, Display, TEXT("\"%s\" laid out %d parameter(s)%s"),
		*GetAssetName(), Collection->GetParameters().Num(),
		Kept > 0 ? *FString::Printf(TEXT(", %d held by the instance"), Kept) : TEXT(""));
}

void INiagaraParameterCollectionImporter::Validate(UObject* Asset) const {
	UNiagaraParameterCollection* Collection = Cast<UNiagaraParameterCollection>(Asset);

	if (Collection == nullptr) return;

	UNiagaraParameterCollectionInstance* Instance = Collection->GetDefaultInstance();

	if (Instance == nullptr) {
		FImportIssues::Report(
			EImportIssue::Data,
			TEXT("The collection has no default instance"),
			FString::Printf(
				TEXT("'%s' keeps its values in an instance beside it, and without one its parameters are names with nothing behind them."),
				*GetAssetName())
		);

		return;
	}

	const FNiagaraParameterStore& Store = Instance->GetParameterStore();

	const int32 Held = Store.GetParameterDataArray().Num();

	int32 Overruns = 0;

	for (const auto& Variable : ReadStoredParameters(Store)) {
		if (Variable.Offset < 0 || Variable.Offset + Variable.GetSizeInBytes() > Held) {
			Overruns++;
		}
	}

	if (Overruns > 0) {
		FImportIssues::Report(
			EImportIssue::Data,
			FString::Printf(TEXT("%d parameter(s) reach past the end of the store"), Overruns),
			FString::Printf(
				TEXT("'%s' holds parameters whose offset and size put them outside the values it carries, so reading one reads whatever follows."),
				*GetAssetName())
		);
	}

	UE_LOG(LogReflection, Display, TEXT("\"%s\" carries %d parameter(s) over %d byte(s)%s"),
		*GetAssetName(), ReadStoredParameters(Store).Num(), Held,
		Overruns > 0 ? TEXT(", not all of which fit") : TEXT(""));
}
