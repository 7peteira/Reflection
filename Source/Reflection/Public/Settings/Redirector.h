#pragma once

#include "CoreMinimal.h"
#include "Redirector.generated.h"

USTRUCT()
struct FRRedirectorPoint {
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Config, Category = RedirectorPoint)
	FString From;

	UPROPERTY(EditAnywhere, Config, Category = RedirectorPoint)
	FString To;
};

USTRUCT()
struct FRRedirector {
	GENERATED_BODY()
public:
	bool IsEnabled() const;

private:
	UPROPERTY(EditAnywhere, Config, Category = Redirector)
	FName Name;

	UPROPERTY(EditAnywhere, Config, Category = Redirector)
	bool Enable = true;

public:
	UPROPERTY(EditAnywhere, Config, Category = Redirector)
	TArray<FRRedirectorPoint> Points;

private:
	UPROPERTY(EditAnywhere, Config, Category = Metadata)
	TArray<FString> Profiles;
};

struct FRRedirects {
	static TMap<FString, TArray<FRRedirectorPoint>> History;

	static void Clear();
	static void Redirect(FString& Path);
	static void Reverse(FString& Path);
};
