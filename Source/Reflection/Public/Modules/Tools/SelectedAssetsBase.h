#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"

#include "Modules/Tools/ToolBase.h"
#include "Modules/Tools/ToolRegistry.h"

class REFLECTION_API TSelectedAssetsBase : public TToolBase {
public:
	virtual void Execute();

	virtual void Process(UObject* Object, const TArray<TSharedPtr<FJsonValue>>& Exports) { }

	virtual FName GetSupportedClass() const { return NAME_None; }

	virtual FText GetDisplayName() const { return FText::GetEmpty(); }
	virtual FText GetTooltip() const { return FText::GetEmpty(); }
	virtual FSlateIcon GetIcon() const { return FSlateIcon(); }

	bool IsRunning() const { return bRunning; }

protected:
	void BrowseToWhenFinished(const UObject* Object);

private:
	void RequestNext();

	void HandleExports(const TArray<TSharedPtr<FJsonValue>>& Exports);
	void Finish();

	TArray<FString> Queue;

	FString PendingBrowseTo;

	int32 QueueIndex = 0;
	bool bRunning = false;
};
