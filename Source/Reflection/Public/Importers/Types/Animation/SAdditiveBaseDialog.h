#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SWindow;
class UAnimSequence;

class REFLECTION_API SAdditiveBaseDialog : public SCompoundWidget {
public:
	SLATE_BEGIN_ARGS(SAdditiveBaseDialog) {}
		SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
		SLATE_ARGUMENT(FString, SequenceName)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static bool Open(const FString& SequenceName, FString& OutBasePath);

private:
	FReply OnUseClicked();
	FReply OnWithoutClicked();

	bool CanUse() const;

	FText GetPickedPath() const;
	FString GetPickedAssetPath() const;

	void OnAssetPicked(const FAssetData& AssetData);
	void OnPathCommitted(const FText& NewText, ETextCommit::Type CommitType);

	TSharedPtr<SWindow> ParentWindow;
	TSharedPtr<SEditableTextBox> PathBox;

	FString SequenceName;

	FString PickedAsset;

	FString TypedPath;

	bool Accepted = false;
};
