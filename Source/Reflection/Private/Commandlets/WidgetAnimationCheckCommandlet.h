#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "WidgetAnimationCheckCommandlet.generated.h"

UCLASS()
class UWidgetAnimationCheckCommandlet : public UCommandlet {
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
