#pragma once

#include "Importers/Constructor/Importer.h"

class IBlueprintImporter : public IImporter {
protected:
	UBlueprint* Blueprint = nullptr;

	struct FDecided {
		FString Owner;
		FName Member;
		UEdGraphPin* Pin = nullptr;
	};

	TArray<FDecided> Decides;

public:
	virtual UObject* CreateAsset(UObject* CreatedAsset = nullptr) override;

	virtual bool Import() override;

	void Answers(const FString& Owner, FName Member, UEdGraphPin* Pin);

protected:
	int32 ConstructVariables();

	void ConstructScript() const;

	int32 ConstructOverriddenComponents();

	void ConstructWidgetTree();

	int32 ConstructGraphs();

	int32 ConstructInterfaces();

	int32 ConstructBody();

	int32 ConstructTimelines();

	int32 ConstructAnimationEvents();

	int32 ConstructComponentEvents();

	int32 MatchEventLayout();

	TMap<FName, int32> AssetEntries;

	TSet<int32> AssetUbergraphStatements;

	TMap<FString, TPair<TWeakObjectPtr<class UK2Node>, FName>> Handouts;

	TMap<FName, TPair<TWeakObjectPtr<class UK2Node>, FName>> Resumes;
};

REGISTER_IMPORTER(IBlueprintImporter, (TArray<FString>{ 
	TEXT("BlueprintGeneratedClass"),
	TEXT("WidgetBlueprintGeneratedClass")
}), TEXT("Blueprints"));
