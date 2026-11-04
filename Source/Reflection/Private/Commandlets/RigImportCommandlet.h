#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RigImportCommandlet.generated.h"

UCLASS()
class URigImportCommandlet : public UCommandlet {
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
