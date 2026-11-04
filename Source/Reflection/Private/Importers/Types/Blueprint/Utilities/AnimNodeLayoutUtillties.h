#pragma once

#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_Root.h"
#include "NodeFactory.h"
#include "SGraphNode.h"
#include "SGraphPin.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/ArrangedChildren.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"
#include "Modules/ModuleManager.h"
#include "Serializers/PropertySerializer.h"

namespace AnimNodeLayout {
	constexpr float ColumnSpacing = 100.f;

	constexpr float RowSpacing = 40.f;

	constexpr float TreeSpacing = 160.f;

	constexpr float FallbackWidth = 220.f;
	constexpr float FallbackHeaderHeight = 60.f;
	constexpr float FallbackPinHeight = 26.f;
}

inline void EnsureAnimGraphNodeFactoryRegistered() {
	static bool bRequested = false;
	if (bRequested) return;

	bRequested = true;
	FModuleManager::Get().LoadModule(TEXT("AnimationBlueprintEditor"));
}

struct FAnimNodeMetrics {
	FVector2D Size = FVector2D::ZeroVector;

	float TopOverhang = 0.f;

	TMap<const UEdGraphPin*, float> PinCenterY;
};

inline void MeasureAnimGraphPinCenters(const TSharedRef<SWidget>& Widget, const FGeometry& Geometry, const TMap<const SWidget*, UEdGraphPin*>& PinWidgets, TMap<const UEdGraphPin*, float>& OutPinCenterY) {
	if (OutPinCenterY.Num() == PinWidgets.Num()) return;

	if (UEdGraphPin* const* Pin = PinWidgets.Find(&Widget.Get())) {
		OutPinCenterY.Add(*Pin, Geometry.GetAbsolutePosition().Y + Geometry.GetLocalSize().Y * 0.5f);

		return;
	}

	FArrangedChildren Children(EVisibility::All);
	Widget->ArrangeChildren(Geometry, Children);

	for (int32 Index = 0; Index < Children.Num(); Index++) {
		MeasureAnimGraphPinCenters(Children[Index].Widget, Children[Index].Geometry, PinWidgets, OutPinCenterY);
	}
}

inline FAnimNodeMetrics MeasureAnimGraphNode(UAnimGraphNode_Base* Node) {
	FAnimNodeMetrics Metrics;
	if (!Node) return Metrics;

	if (FSlateApplication::IsInitialized()) {
		EnsureAnimGraphNodeFactoryRegistered();

		if (const TSharedPtr<SGraphNode> NodeWidget = FNodeFactory::CreateNodeWidget(Node)) {
			NodeWidget->SlatePrepass(1.f);

			const FVector2D DesiredSize = NodeWidget->GetDesiredSize();
			if (DesiredSize.X > 1.f && DesiredSize.Y > 1.f) {
				Metrics.Size = DesiredSize;

				const FGeometry NodeGeometry = FGeometry::MakeRoot(DesiredSize, FSlateLayoutTransform());

				FArrangedChildren Zones(EVisibility::All);
				NodeWidget->ArrangeChildren(NodeGeometry, Zones);

				for (int32 Index = 0; Index < Zones.Num(); Index++) {
					Metrics.TopOverhang = FMath::Max(Metrics.TopOverhang, -Zones[Index].Geometry.GetAbsolutePosition().Y);
				}

				TMap<const SWidget*, UEdGraphPin*> PinWidgets;
				for (UEdGraphPin* Pin : Node->Pins) {
					if (!Pin) continue;

					if (const TSharedPtr<SGraphPin> PinWidget = NodeWidget->FindWidgetForPin(Pin)) {
						PinWidgets.Add(PinWidget.Get(), Pin);
					}
				}

				if (PinWidgets.Num() > 0) {
					MeasureAnimGraphPinCenters(NodeWidget.ToSharedRef(), NodeGeometry, PinWidgets, Metrics.PinCenterY);
				}

				return Metrics;
			}
		}
	}

	int32 InputRows = 0;
	int32 OutputRows = 0;

	for (const UEdGraphPin* Pin : Node->Pins) {
		if (!Pin || Pin->bHidden) continue;

		int32& Row = Pin->Direction == EGPD_Input ? InputRows : OutputRows;
		Metrics.PinCenterY.Add(Pin, AnimNodeLayout::FallbackHeaderHeight + (Row + 0.5f) * AnimNodeLayout::FallbackPinHeight);

		Row++;
	}

	Metrics.Size = FVector2D(
		AnimNodeLayout::FallbackWidth,
		AnimNodeLayout::FallbackHeaderHeight + FMath::Max(InputRows, OutputRows) * AnimNodeLayout::FallbackPinHeight
	);

	return Metrics;
}

struct FAnimGraphLayoutContext {
	TMap<UAnimGraphNode_Base*, FAnimNodeMetrics> Metrics;
	TMap<UAnimGraphNode_Base*, TArray<UAnimGraphNode_Base*>> Inputs;

	TMap<UAnimGraphNode_Base*, int32> Depths;

	TArray<float> ColumnLeft;
	TArray<float> ColumnWidth;

	TArray<float> ColumnNextY;

	TMap<UAnimGraphNode_Base*, float> PlacedTop;

	float NextLeafY = 0.f;
};

inline float GetAnimPinCenterY(const FAnimGraphLayoutContext& Context, UAnimGraphNode_Base* Node, const UEdGraphPin* Pin) {
	const FAnimNodeMetrics& Metrics = Context.Metrics.FindChecked(Node);

	if (const float* CenterY = Metrics.PinCenterY.Find(Pin)) {
		return *CenterY;
	}

	return Metrics.Size.Y * 0.5f;
}

inline void CollectAnimNodeTree(UAnimGraphNode_Base* Node, const int32 Depth, FAnimGraphLayoutContext& Context, TSet<UAnimGraphNode_Base*>& Claimed, TArray<UAnimGraphNode_Base*>& OutTree, TSet<UAnimGraphNode_Base*>& Stack) {
	if (const int32* Existing = Context.Depths.Find(Node)) {
		if (*Existing >= Depth) return;

		Context.Depths.Add(Node, Depth);
	} else {
		if (Claimed.Contains(Node)) return;

		Claimed.Add(Node);
		OutTree.Add(Node);
		Context.Depths.Add(Node, Depth);
	}

	if (Stack.Contains(Node)) return;
	Stack.Add(Node);

	if (const TArray<UAnimGraphNode_Base*>* Children = Context.Inputs.Find(Node)) {
		for (UAnimGraphNode_Base* Child : *Children) {
			CollectAnimNodeTree(Child, Depth + 1, Context, Claimed, OutTree, Stack);
		}
	}

	Stack.Remove(Node);
}

inline void ShiftAnimNodeSubtree(UAnimGraphNode_Base* Node, const float Delta, FAnimGraphLayoutContext& Context, TSet<UAnimGraphNode_Base*>& Shifted) {
	if (Shifted.Contains(Node)) return;

	float* Top = Context.PlacedTop.Find(Node);
	if (!Top) return;

	int32 PlacedConsumers = 0;
	bool bHasInputs = false;

	for (UEdGraphPin* Pin : Node->Pins) {
		if (!Pin) continue;

		for (UEdGraphPin* LinkedPin : Pin->LinkedTo) {
			if (!LinkedPin) continue;

			UAnimGraphNode_Base* Linked = Cast<UAnimGraphNode_Base>(LinkedPin->GetOwningNode());
			if (!Linked || !Context.PlacedTop.Contains(Linked)) continue;

			if (Pin->Direction == EGPD_Input) {
				bHasInputs = true;
			} else {
				PlacedConsumers++;
			}
		}
	}

	if (PlacedConsumers > 1) return;

	Shifted.Add(Node);

	*Top += Delta;
	Node->NodePosY = FMath::RoundToInt(*Top);

	const float Bottom = *Top + Context.Metrics.FindChecked(Node).Size.Y + AnimNodeLayout::RowSpacing;

	float& NextY = Context.ColumnNextY[Context.Depths.FindChecked(Node)];
	NextY = FMath::Max(NextY, Bottom);

	if (!bHasInputs) {
		Context.NextLeafY = FMath::Max(Context.NextLeafY, Bottom);
		return;
	}

	for (UEdGraphPin* Pin : Node->Pins) {
		if (!Pin || Pin->Direction != EGPD_Input) continue;

		for (UEdGraphPin* LinkedPin : Pin->LinkedTo) {
			if (!LinkedPin) continue;

			if (UAnimGraphNode_Base* Child = Cast<UAnimGraphNode_Base>(LinkedPin->GetOwningNode())) {
				ShiftAnimNodeSubtree(Child, Delta, Context, Shifted);
			}
		}
	}
}

inline float PlaceAnimGraphNode(UAnimGraphNode_Base* Node, FAnimGraphLayoutContext& Context) {
	if (const float* Existing = Context.PlacedTop.Find(Node)) {
		return *Existing;
	}

	const int32 Depth = Context.Depths.FindChecked(Node);

	const FAnimNodeMetrics& Metrics = Context.Metrics.FindChecked(Node);
	const FVector2D Size = Metrics.Size;
	const float Overhang = Metrics.TopOverhang;

	Context.PlacedTop.Add(Node, 0.f);

	float FirstTop = 0.f;
	float LastTop = 0.f;
	int32 LinkCount = 0;

	for (UEdGraphPin* Pin : Node->Pins) {
		if (!Pin || Pin->Direction != EGPD_Input) continue;

		for (UEdGraphPin* LinkedPin : Pin->LinkedTo) {
			if (!LinkedPin) continue;

			UAnimGraphNode_Base* Child = Cast<UAnimGraphNode_Base>(LinkedPin->GetOwningNode());

			if (!Child || !Context.Depths.Contains(Child)) continue;

			const float ChildTop = PlaceAnimGraphNode(Child, Context);

			const float AlignedTop = ChildTop + GetAnimPinCenterY(Context, Child, LinkedPin) - GetAnimPinCenterY(Context, Node, Pin);

			if (LinkCount == 0) FirstTop = AlignedTop;
			LastTop = AlignedTop;

			LinkCount++;
		}
	}

	float Top;

	if (LinkCount > 0) {
		Top = (FirstTop + LastTop) * 0.5f;
	} else {
		Top = Context.NextLeafY + Overhang;
	}

	const float PushedTop = FMath::Max(Top, Context.ColumnNextY[Depth] + Overhang);
	const float Pushed = PushedTop - Top;

	Top = PushedTop;

	const float Bottom = Top + Size.Y + AnimNodeLayout::RowSpacing;
	Context.ColumnNextY[Depth] = Bottom;

	if (LinkCount == 0) {
		Context.NextLeafY = Bottom;
	}

	const float X = Context.ColumnLeft[Depth] + (Context.ColumnWidth[Depth] - Size.X);

	Node->NodePosX = FMath::RoundToInt(X);
	Node->NodePosY = FMath::RoundToInt(Top);

	Context.PlacedTop.Add(Node, Top);

	if (Pushed > 0.f && LinkCount > 0) {
		TSet<UAnimGraphNode_Base*> Shifted;
		Shifted.Add(Node);

		for (UEdGraphPin* Pin : Node->Pins) {
			if (!Pin || Pin->Direction != EGPD_Input) continue;

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo) {
				if (!LinkedPin) continue;

				if (UAnimGraphNode_Base* Child = Cast<UAnimGraphNode_Base>(LinkedPin->GetOwningNode())) {
					ShiftAnimNodeSubtree(Child, Pushed, Context, Shifted);
				}
			}
		}
	}

	return Top;
}

inline void LayoutAnimGraphTree(UAnimGraphNode_Base* Root, FAnimGraphLayoutContext& Context, TSet<UAnimGraphNode_Base*>& Claimed, float& TreeTop) {
	Context.Depths.Reset();
	Context.ColumnLeft.Reset();
	Context.ColumnWidth.Reset();
	Context.ColumnNextY.Reset();
	Context.PlacedTop.Reset();
	Context.NextLeafY = 0.f;

	TArray<UAnimGraphNode_Base*> TreeNodes;
	TSet<UAnimGraphNode_Base*> Stack;

	CollectAnimNodeTree(Root, 0, Context, Claimed, TreeNodes, Stack);
	if (TreeNodes.Num() == 0) return;

	int32 MaxDepth = 0;
	for (UAnimGraphNode_Base* Node : TreeNodes) {
		MaxDepth = FMath::Max(MaxDepth, Context.Depths.FindChecked(Node));
	}

	Context.ColumnWidth.SetNumZeroed(MaxDepth + 1);
	for (UAnimGraphNode_Base* Node : TreeNodes) {
		const int32 Depth = Context.Depths.FindChecked(Node);
		Context.ColumnWidth[Depth] = FMath::Max(Context.ColumnWidth[Depth], Context.Metrics.FindChecked(Node).Size.X);
	}

	Context.ColumnLeft.SetNumZeroed(MaxDepth + 1);
	for (int32 Depth = 1; Depth <= MaxDepth; Depth++) {
		Context.ColumnLeft[Depth] = Context.ColumnLeft[Depth - 1] - AnimNodeLayout::ColumnSpacing - Context.ColumnWidth[Depth];
	}

	Context.ColumnNextY.Init(TNumericLimits<float>::Lowest(), MaxDepth + 1);

	PlaceAnimGraphNode(Root, Context);

	for (UAnimGraphNode_Base* Node : TreeNodes) {
		if (!Context.PlacedTop.Contains(Node)) {
			PlaceAnimGraphNode(Node, Context);
		}
	}

	float MinY = TNumericLimits<float>::Max();
	float MaxY = TNumericLimits<float>::Lowest();

	for (UAnimGraphNode_Base* Node : TreeNodes) {
		const float Top = Context.PlacedTop.FindChecked(Node);
		const FAnimNodeMetrics& Metrics = Context.Metrics.FindChecked(Node);

		MinY = FMath::Min(MinY, Top - Metrics.TopOverhang);
		MaxY = FMath::Max(MaxY, Top + Metrics.Size.Y);
	}

	const int32 Offset = FMath::RoundToInt(TreeTop - MinY);
	for (UAnimGraphNode_Base* Node : TreeNodes) {
		Node->NodePosY += Offset;
	}

	TreeTop += MaxY - MinY + AnimNodeLayout::TreeSpacing;
}

inline void AutoLayoutAnimGraphNodes(const TArray<FUObjectExport*>& NodeExports) {
	FAnimGraphLayoutContext Context;

	TArray<UAnimGraphNode_Base*> Nodes;
	TArray<UAnimGraphNode_Base*> Roots;

	for (const FUObjectExport* Export : NodeExports) {
		UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(Export->Object);
		if (!Node) continue;

		Nodes.Add(Node);
		Context.Metrics.Add(Node, MeasureAnimGraphNode(Node));

		TArray<UAnimGraphNode_Base*>& Inputs = Context.Inputs.Add(Node);
		bool bHasOutputs = false;

		for (UEdGraphPin* Pin : Node->Pins) {
			if (!Pin) continue;

			if (Pin->Direction == EGPD_Input) {
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo) {
					if (!LinkedPin) continue;

					if (UAnimGraphNode_Base* LinkedNode = Cast<UAnimGraphNode_Base>(LinkedPin->GetOwningNode())) {
						Inputs.AddUnique(LinkedNode);
					}
				}
			} else if (Pin->LinkedTo.Num() > 0) {
				bHasOutputs = true;
			}
		}

		if (!bHasOutputs) {
			Roots.Add(Node);
		}
	}

	if (Nodes.Num() == 0) return;

	Roots.Sort([](const UAnimGraphNode_Base& A, const UAnimGraphNode_Base& B) {
		return A.IsA<UAnimGraphNode_Root>() && !B.IsA<UAnimGraphNode_Root>();
	});

	TSet<UAnimGraphNode_Base*> Claimed;
	float TreeTop = 0.f;

	for (UAnimGraphNode_Base* Root : Roots) {
		LayoutAnimGraphTree(Root, Context, Claimed, TreeTop);
	}

	for (UAnimGraphNode_Base* Node : Nodes) {
		if (!Claimed.Contains(Node)) {
			LayoutAnimGraphTree(Node, Context, Claimed, TreeTop);
		}
	}
}
