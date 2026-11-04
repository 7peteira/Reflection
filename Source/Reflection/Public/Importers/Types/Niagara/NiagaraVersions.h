#pragma once

#include "CoreMinimal.h"

struct FNiagaraVersion {
	void* Data = nullptr;

	UScriptStruct* Struct = nullptr;

	bool IsValid() const { return Data != nullptr && Struct != nullptr; }
};

class REFLECTION_API FNiagaraVersions {
public:
	static FNiagaraVersion Only(UObject* Asset);

	static TArray<FNiagaraVersion> All(UObject* Asset);

	static UObject* GetObject(const FNiagaraVersion& Version, const TCHAR* Field);

	static UObject* GetObjectIn(const FNiagaraVersion& Version, const TCHAR* Field, const TCHAR* Inner);

	static bool HasObject(const FNiagaraVersion& Version, const TCHAR* Field);

	static int32 NumObjects(const FNiagaraVersion& Version, const TCHAR* Field);

	static bool SetObject(const FNiagaraVersion& Version, const TCHAR* Field, UObject* Value);

	static bool SetObjectIn(const FNiagaraVersion& Version, const TCHAR* Field, const TCHAR* Inner, UObject* Value);

	static int32 SetObjects(const FNiagaraVersion& Version, const TCHAR* Field, const TArray<UObject*>& Values);

	static UObject* MakeObject(const FNiagaraVersion& Version, const TCHAR* Field, UObject* Outer);
};
