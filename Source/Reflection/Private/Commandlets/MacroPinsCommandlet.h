#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "MacroPinsCommandlet.generated.h"

UCLASS()
class UMacroPinsCommandlet : public UCommandlet {
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
