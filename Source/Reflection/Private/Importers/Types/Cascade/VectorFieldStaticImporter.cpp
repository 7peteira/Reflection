#include "Engine/SharedPtrCompatibility.h"
#include "Importers/Types/Cascade/VectorFieldStaticImporter.h"

#include "VectorField/VectorFieldStatic.h"
#include "Math/Float16Color.h"

#include "Engine/Log.h"
#include "Modules/Cloud/Cloud.h"

UObject* IVectorFieldStaticImporter::CreateAsset(UObject* CreatedAsset) {
	return IImporter::CreateAsset(NewObject<UVectorFieldStatic>(GetPackage(), UVectorFieldStatic::StaticClass(), *GetAssetName(), RF_Public | RF_Standalone));
}

bool IVectorFieldStaticImporter::Import() {
	UVectorFieldStatic* VectorField = Create<UVectorFieldStatic>();

	GetObjectSerializer()->DeserializeObjectProperties(GetAssetData(), VectorField);

	const int64 VoxelCount = static_cast<int64>(VectorField->SizeX) * VectorField->SizeY * VectorField->SizeZ;

	if (VoxelCount <= 0 || VoxelCount > static_cast<int64>(MAX_int32) / static_cast<int64>(sizeof(FFloat16Color))) {
		UE_LOG(LogReflection, Error, TEXT("\"%s\" has an unusable vector field grid of %dx%dx%d."), *GetAssetName(), VectorField->SizeX, VectorField->SizeY, VectorField->SizeZ);

		return false;
	}

	const int32 VolumeSize = static_cast<int32>(VoxelCount * sizeof(FFloat16Color));

	TArray<uint8> VolumeData;

	if (!DownloadVolumeData(VolumeSize, VolumeData)) {
		UE_LOG(LogReflection, Warning, TEXT("No vector data for \"%s\", reflected as an empty field."), *GetAssetName());

		VolumeData.SetNumZeroed(VolumeSize);
	}

	VectorField->SourceData.Lock(LOCK_READ_WRITE); {
		void* Destination = VectorField->SourceData.Realloc(VolumeSize);

		FMemory::Memcpy(Destination, VolumeData.GetData(), VolumeSize);
	}
	VectorField->SourceData.Unlock();

	return OnAssetCreation(VectorField);
}

bool IVectorFieldStaticImporter::DownloadVolumeData(const int32 ExpectedSize, TArray<uint8>& OutData) const {
	if (!Cloud::Status::IsOpened()) {
		return false;
	}

	FString FetchPath = GetPackage()->GetPathName(); {
		FRRedirects::Reverse(FetchPath);
	}

	if (!Cloud::Export::GetBinaryBlocking(FetchPath, TEXT("application/octet-stream"), OutData)) {
		return false;
	}

	if (OutData.Num() != ExpectedSize) {
		UE_LOG(LogReflection, Warning, TEXT("Vector data for \"%s\" is %d bytes, expected %d."), *GetAssetName(), OutData.Num(), ExpectedSize);

		return false;
	}

	return true;
}
