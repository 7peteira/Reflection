#pragma once

struct REFLECTION_API FAssetUtilities {
public:
	static UPackage* CreateAssetPackage(const FString& FullPath);
	static UPackage* CreateAssetPackage(const FString& Name, const FString& OutputPath);
	static UPackage* CreateAssetPackage(const FString& Name, const FString& OutputPath, FString& FailureReason);

public:
	static inline bool bImportReferences = true;

	static inline TSet<FString> ReflectedThisRun;

	static inline int32 ImportDepth = 0;

	struct REFLECTION_API FRunScope {
		FRunScope() {
			if (ImportDepth++ == 0) ReflectedThisRun.Empty();
		}

		~FRunScope() {
			if (--ImportDepth == 0) ReflectedThisRun.Empty();
		}
	};

	template <class T = UObject>
	static bool ConstructAsset(const FString& Path, const FString& RealPath, const FString& Type, TObjectPtr<T>& OutObject, bool& bSuccess);

	static bool Fast_Construct_TypeTexture(const TSharedPtr<FJsonObject>& JsonExport, const FString& Path, const FString& Type, TArray<uint8> Data, UTexture*& OutTexture);
};
