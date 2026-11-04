#pragma once

#include "CoreMinimal.h"
#include "Serialization/JsonSerializer.h"
#include "Settings/ReflectionSettings.h"
#include "Modules/Cloud/Remote.h"

class REFLECTION_API Cloud {
public:
	static inline FString URL = TEXT("http://localhost:1500");

public:
	class REFLECTION_API Status {
	public:
		static bool IsOpened();

		static void IsReady(TFunction<void(bool)> OnResponse);

		static void NotifyNotRunning();

		static void Check(TFunction<void(bool)> OnResponse);
	};

public:
	static inline FString MetadataURL = TEXT("/api/metadata");

	static void Update(TFunction<void(bool)> OnResponse);

	static bool EnsureMetadataBlocking();

public:
	static inline FString ExportURL = TEXT("/api/export");
	static inline FString SkinWeightsURL = TEXT("/api/export/skinweights");
	static inline FString LodModelURL = TEXT("/api/export/lodmodel");
	static inline FString MorphTargetsURL = TEXT("/api/export/morphtargets");
	static inline FString StaticMeshURL = TEXT("/api/export/staticmesh");
	static inline FString DnaURL = TEXT("/api/export/dna");
	static inline FString ReferenceSkeletonURL = TEXT("/api/export/refskeleton");
	static inline FString ExpressionsURL = TEXT("/api/export/expressions");
	static inline FString FontFaceURL = TEXT("/api/export/fontface");
	static inline FString DnaPosesURL = TEXT("/api/export/dnaposes");
	static inline FString DnaMorphsURL = TEXT("/api/export/dnamorphs");
	static inline FString CurveMappingURL = TEXT("/api/export/curvemapping");

	class REFLECTION_API Export {
	public:
		static void GetAsync(const FString& Path, bool Raw, TMap<FString, FString> Parameters, const TMap<FString, FString>& Headers, TFunction<void(TSharedPtr<FJsonObject>)> OnResponse);
		static void GetRawAsync(const FString& Path, TFunction<void(TSharedPtr<FJsonObject>)> OnResponse);

		static void GetRawExportsAsync(const FString& Path, TFunction<void(const TArray<TSharedPtr<FJsonValue>>&)> OnResponse);

		static TSharedPtr<FJsonObject> GetBlocking(const FString& Path, bool Raw, TMap<FString, FString> Parameters = {}, const TMap<FString, FString>& Headers = {});
		static TSharedPtr<FJsonObject> GetRawBlocking(const FString& Path, const TMap<FString, FString>& Parameters = {}, const TMap<FString, FString>& Headers = {});

		static bool GetBinaryBlocking(const FString& Path, const FString& ContentType, TArray<uint8>& OutData, const FString& Named = FString());

		static TArray<TSharedPtr<FJsonValue>> GetSkinWeightsBlocking(const FString& Path);

		static TSharedPtr<FJsonObject> GetLodModelBlocking(const FString& Path);

		static TSharedPtr<FJsonObject> GetMorphTargetsBlocking(const FString& Path);

		static TSharedPtr<FJsonObject> GetStaticMeshBlocking(const FString& Path, const FString& Named = FString());

		static TArray<uint8> GetDnaBlocking(const FString& Path);

		static TSharedPtr<FJsonObject> GetReferenceSkeletonBlocking(const FString& Path);

		static TSharedPtr<FJsonObject> GetCurveExpressionsBlocking(const FString& Path);

		static TArray<uint8> GetFontFaceBlocking(const FString& Path);

		static TSharedPtr<FJsonObject> GetDnaPosesBlocking(const FString& Path, bool bBackport = false);

		static TSharedPtr<FJsonObject> GetCurveMappingBlocking();

		static TSharedPtr<FJsonObject> GetDnaMorphsBlocking(const FString& Path, bool bBackport = false);
	};

public:
	static inline FString FolderPathsURL = TEXT("/api/folder/paths");

	class REFLECTION_API Folder {
	public:
		static TArray<FString> GetPathsBlocking(const FString& Path);
	};

public:
	static void Get(
		const FString& RequestURL,
		const TMap<FString, FString>& Parameters,
		const TMap<FString, FString>& Headers,
		TFunction<void(TSharedPtr<FJsonObject>)> OnComplete
	);

	static void Post(
		const FString& RequestURL,
		const FString& Body,
		const TMap<FString, FString>& Headers,
		TFunction<void(TSharedPtr<FJsonObject>, int32)> OnComplete
	);

	static TSharedPtr<FJsonObject> GetBlocking(const FString& RequestURL, const TMap<FString, FString>& Parameters = {}, const TMap<FString, FString>& Headers = {});

	static void RunWhenSafe(TFunction<void()> Work);

private:
	static FReflectionHttpRequest BuildRequest(const FString& RequestURL, const TMap<FString, FString>& Parameters, const TMap<FString, FString>& Headers);

	static TSharedPtr<FJsonObject> ParseResponse(const FReflectionHttpResponse& Response);
};
