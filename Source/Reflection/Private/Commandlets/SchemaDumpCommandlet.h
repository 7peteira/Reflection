#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "SchemaDumpCommandlet.generated.h"

UCLASS()
class USchemaDumpCommandlet : public UCommandlet {
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
