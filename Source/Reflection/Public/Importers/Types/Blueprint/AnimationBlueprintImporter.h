#pragma once

#include "Importers/Types/Blueprint/BlueprintImporter.h"

class UAnimGraphNode_BlendListByEnum;
class UAnimGraphNode_Base;

#if UE4_25_BELOW
class UAnimBlueprint;
#endif

class IAnimationBlueprintImporter final : public IBlueprintImporter {
public:
	virtual bool Import() override;

private:
	UAnimBlueprint* CreateAnimBlueprint(UClass* ParentClass);

	int32 ConstructBlendOptions();

	void ProcessEvaluateGraphExposedInputs(const TSharedPtr<FJsonObject>& AnimNodeProperties) const;

	static UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBlueprint);

	void CreateGraph(const TSharedPtr<FJsonObject>& AnimNodeProperties, UEdGraph* AnimGraph, FUObjectExportContainer* Container);

	static void CreateAnimGraphNodes(UEdGraph* AnimGraph, const TSharedPtr<FJsonObject>& AnimNodeProperties, FUObjectExportContainer& OutContainer);

	static void AddNodesToGraph(UEdGraph* AnimGraph, FUObjectExportContainer* Container);

	void HandleNodeDeserialization(FUObjectExportContainer* Container);

	static void ConnectAnimGraphNodes(FUObjectExportContainer* Container, UEdGraph* AnimGraph);

	void ReadFolded(const TSharedPtr<FJsonObject>& Nodes, const TArray<FString>& Keys);

	void Unfold(const TSharedPtr<FJsonObject>& Nodes, const TArray<FString>& Keys) const;

	FName MemberAt(const FString& Key, int32 Which, uint32 Entry) const;

	FString NodeAt(const TArray<FString>& Keys, uint32 Entry, int32& Which) const;

	void ReadBindings(const TSharedPtr<FJsonObject>& Nodes, const TArray<FString>& Keys);

public:
	void Hands(const FString& Key, FName Member, const TArray<FString>& Path, bool bTurned = false);

	void ApplyBindings(const FString& Key, UAnimGraphNode_Base* Node) const;

protected:
	void ExposeHandedOver(UAnimGraphNode_Base* Node, const TArray<FString>& Named) const;

	void ShowBoundPins(const TSharedPtr<FJsonObject>& AnimNodeProperties, FUObjectExportContainer* Container) const;

	void Draw(UAnimGraphNode_Base* Node, FName Member, const FString& Named, bool bTurned, int32 Row) const;

	bool Expose(UAnimGraphNode_Base* Node, FName Member) const;

	void AnswerThrough(FUObjectExportContainer* Container);

	void DrawBindings(FUObjectExportContainer* Container) const;

	struct FHandedOver {
		FName Member;
		TArray<FString> Path;
		bool bTurned = false;
	};

	TMap<FString, TArray<FHandedOver>> Bindings;

	TArray<TSharedPtr<FJsonValue>> Constants;
	TArray<TSharedPtr<FJsonValue>> Mutables;
	TMap<FString, int32> MutableAt;
	TSharedPtr<FJsonObject> AnimMutables;
	TMap<FString, TMap<FString, int32>> Slots;
	TMap<int32, TArray<uint32>> Folded;
	TMap<FString, FString> Kinds;

	void SettleCachedPoses();

	void SettleInputPoses(UEdGraph* Layer) const;

	TArray<FUObjectExportContainer*> AnimNodeContainers;

	TArray<TPair<TWeakObjectPtr<class UAnimGraphNode_UseCachedPose>, FString>> Borrowed;

	static void UpdateBlendListByEnumVisibleEntries(FUObjectExport* NodeExport, FUObjectExportContainer* Container, UEdGraph* AnimGraph);

protected:
	UAnimBlueprint* AnimBlueprint = nullptr;

	TArray<FString> NodesKeys;
	TArray<FString> ReversedNodesKeys;

	TArray<TSharedPtr<FJsonValue>> BakedStateMachines;

	TSharedPtr<FJsonObject> RootAnimNodeProperties;
	FUObjectExportContainer* RootAnimNodeContainer = new FUObjectExportContainer();

	TSharedPtr<FJsonObject> SerializedSparseClassData;

	TArray<FString> SyncGroupNames;
};

REGISTER_IMPORTER(IAnimationBlueprintImporter, (TArray<FString>{ 
	TEXT("AnimBlueprintGeneratedClass")
}), TEXT("Blueprints"));
