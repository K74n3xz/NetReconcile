#include "CharacterObservationPanel.h"
#include "../Character/LocalPlayerObservationSubsystem.h"
#include "../Character/ObservationCharacter.h"
#include "../Network/NetworkFaultSimulationSubsystem.h"
#include "../Network/ConnectionObservationSubsystem.h"
#include "../ObservationPlayerController.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/GridPanel.h"
#include "Components/GridSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/LocalPlayer.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

namespace
{
	const FString Unavailable = TEXT("\u2014");
	struct FFaultField
	{
		const TCHAR* Name;
		const TCHAR* Label;
		const TCHAR* Hint;
		int32 FNetworkFaultSimulationSettings::* Member;
	};
	const FFaultField FaultFields[] = {
		{TEXT("PktLag"), TEXT("Send latency (ms)"), TEXT("Fixed latency; ignores the min/max range. Reordering and frame delay take priority."), &FNetworkFaultSimulationSettings::PktLag},
		{TEXT("PktLagVariance"), TEXT("Send variance (+/- ms)"), TEXT("Variation around fixed latency, or each of the two jitter latency levels."), &FNetworkFaultSimulationSettings::PktLagVariance},
		{TEXT("PktLagMin"), TEXT("Send minimum (ms)"), TEXT("Lower bound of random latency, and baseline for jitter."), &FNetworkFaultSimulationSettings::PktLagMin},
		{TEXT("PktLagMax"), TEXT("Send maximum (ms)"), TEXT("Upper bound of random latency; raised to the minimum when needed."), &FNetworkFaultSimulationSettings::PktLagMax},
		{TEXT("PktJitter"), TEXT("Send jitter (ms)"), TEXT("Alternates between minimum and minimum + jitter; takes priority over fixed/range latency and may cause received packet loss."), &FNetworkFaultSimulationSettings::PktJitter},
		{TEXT("PktLoss"), TEXT("Send loss (%)"), TEXT("Clamped to 0-100. Works with the other outgoing settings."), &FNetworkFaultSimulationSettings::PktLoss},
		{TEXT("PktLossMinSize"), TEXT("Loss minimum (bytes)"), TEXT("Only packets strictly larger than this size can be dropped."), &FNetworkFaultSimulationSettings::PktLossMinSize},
		{TEXT("PktLossMaxSize"), TEXT("Loss maximum (bytes)"), TEXT("Only packets strictly smaller than this size can be dropped; 0 removes the upper limit."), &FNetworkFaultSimulationSettings::PktLossMaxSize},
		{TEXT("PktOrder"), TEXT("Reorder (0/1)"), TEXT("Random reordering takes priority over duplication and latency. Frame delay takes priority over reordering."), &FNetworkFaultSimulationSettings::PktOrder},
		{TEXT("PktDup"), TEXT("Duplicate (%)"), TEXT("Clamped to 0-100; applies to packets sent without outgoing delay or reordering."), &FNetworkFaultSimulationSettings::PktDup},
		{TEXT("PktFrameDelay"), TEXT("Send delay (ticks)"), TEXT("Takes priority over the other outgoing delays, reordering, duplication and buffer bloat. Loss still applies."), &FNetworkFaultSimulationSettings::PktFrameDelay},
		{TEXT("PktBufferBloatInMS"), TEXT("Send buffer bloat (ms)"), TEXT("Batches outgoing packets during the specified interval; combines with millisecond latency."), &FNetworkFaultSimulationSettings::PktBufferBloatInMS},
		{TEXT("PktIncomingLagMin"), TEXT("Receive minimum (ms)"), TEXT("Minimum delay before processing received packets. Millisecond latency takes priority over tick delay."), &FNetworkFaultSimulationSettings::PktIncomingLagMin},
		{TEXT("PktIncomingLagMax"), TEXT("Receive maximum (ms)"), TEXT("Maximum delay before processing received packets; raised to the minimum when needed."), &FNetworkFaultSimulationSettings::PktIncomingLagMax},
		{TEXT("PktIncomingLoss"), TEXT("Receive loss (%)"), TEXT("Clamped to 0-100; incoming packets are dropped before applying delay."), &FNetworkFaultSimulationSettings::PktIncomingLoss},
		{TEXT("PktIncomingFrameDelay"), TEXT("Receive delay (ticks)"), TEXT("Used when receive millisecond latency is disabled; takes priority over incoming buffer bloat."), &FNetworkFaultSimulationSettings::PktIncomingFrameDelay},
		{TEXT("PktIncomingBufferBloatInMS"), TEXT("Receive buffer bloat (ms)"), TEXT("Batches incoming packets during the specified interval; combines with receive millisecond latency."), &FNetworkFaultSimulationSettings::PktIncomingBufferBloatInMS}
	};
	const TCHAR* ScenarioNames[] = {TEXT("Burst loss"), TEXT("Short blackout"), TEXT("Latency spike"), TEXT("Bandwidth limit")};
	struct FScenarioField
	{
		const TCHAR* Name;
		const TCHAR* Label;
		float FNetworkFaultScenarioSettings::* Member;
	};
	const FScenarioField ScenarioFields[] = {
		{TEXT("RepeatInterval"), TEXT("Cycle interval (s)"), &FNetworkFaultScenarioSettings::RepeatIntervalSeconds},
		{TEXT("Duration"), TEXT("Fault duration (s)"), &FNetworkFaultScenarioSettings::DurationSeconds},
		{TEXT("AdditionalDelay"), TEXT("Additional delay (ms)"), &FNetworkFaultScenarioSettings::AdditionalDelayMs},
		{TEXT("Bandwidth"), TEXT("Packet bandwidth (Kbps)"), &FNetworkFaultScenarioSettings::BandwidthKbps}
	};
	FString Number(double Value) { return FString::Printf(TEXT("%.2f"), Value); }
	const FLinearColor Surface(0.025f, 0.035f, 0.052f, 0.98f);
	const FLinearColor CardSurface(0.045f, 0.057f, 0.080f, 1.0f);
	const FLinearColor Muted(0.58f, 0.66f, 0.75f);
	const FLinearColor ServerColor(1.0f, 0.31f, 0.32f);
	const FLinearColor PredictionColor(0.26f, 0.66f, 1.0f);
	const FLinearColor SimulationColor(0.20f, 0.90f, 0.90f);
	FName Key(FName Prefix, const TCHAR* Suffix) { return FName(Prefix.ToString() + Suffix); }
	bool IsCommon(FName Field)
	{
		return Field == TEXT("PktLag") || Field == TEXT("PktLoss") || Field == TEXT("PktIncomingLagMin")
			|| Field == TEXT("PktIncomingLagMax") || Field == TEXT("PktIncomingLoss");
	}
	double UnackedMilliseconds(const FPawnObservationSnapshot& Snapshot)
	{
		const FAutonomousPawnObservation* Data = Snapshot.Get() ? Snapshot.Get()->GetAutonomous() : nullptr;
		return Data ? Data->GetUnacknowledgedDurationSeconds() * 1000.0 : 0.0;
	}

	FString VectorText(const FVector& Vector)
	{
		return FString::Printf(TEXT("X %.1f   Y %.1f   Z %.1f"), Vector.X, Vector.Y, Vector.Z);
	}
	const TCHAR* RoleName(EObservedPawnRole Role)
	{
		switch (Role)
		{
		case EObservedPawnRole::Authority: return TEXT("Authority");
		case EObservedPawnRole::AutonomousProxy: return TEXT("Autonomous Proxy");
		case EObservedPawnRole::SimulatedProxy: return TEXT("Simulated Proxy");
		default: return TEXT("Unknown");
		}
	}
	double ReferenceAgeMs(const FPawnObservationSnapshot& Snapshot)
	{
		const FSimulatedPawnObservation* Data = Snapshot.Get() ? Snapshot.Get()->GetSimulated() : nullptr;
		return Data ? FMath::Max(0.0, Snapshot.Get()->GetCommon().SampleTime.RealTimeSeconds - Data->ReferenceReceivedAt.RealTimeSeconds) * 1000.0 : 0.0;
	}
	FString Availability(EObservationAvailability Status)
	{
		switch (Status)
		{
		case EObservationAvailability::Available: return TEXT("Available");
		case EObservationAvailability::AwaitingSample: return TEXT("Waiting for a source sample");
		case EObservationAvailability::MissingSource: return TEXT("Source missing");
		case EObservationAvailability::Unsupported: return TEXT("Source unsupported");
		default: return TEXT("Not applicable");
		}
	}
	FString PairingName(EObservedMovePairing Pairing)
	{
		switch (Pairing)
		{
		case EObservedMovePairing::Paired: return TEXT("Paired");
		case EObservedMovePairing::MissingPayload: return TEXT("Missing observation payload");
		case EObservedMovePairing::UnsupportedProtocol: return TEXT("Packed observation protocol unavailable");
		case EObservedMovePairing::MoveIdMismatch: return TEXT("Move ID mismatch");
		case EObservedMovePairing::StaleServerSample: return TEXT("Stale server sample");
		case EObservedMovePairing::InvalidPayload: return TEXT("Invalid observation payload");
		default: return TEXT("Waiting for a response");
		}
	}
	FString MovementName(const FObservedMotion& State)
	{
		if (State.MovementModeStatus != EObservationAvailability::Available) { return Availability(State.MovementModeStatus); }
		return FString::Printf(TEXT("%s \u00b7 custom %d \u00b7 ground %s"),
			*StaticEnum<EMovementMode>()->GetDisplayNameTextByValue(State.MovementMode.GetValue()).ToString(), State.CustomMovementMode,
			*StaticEnum<EMovementMode>()->GetDisplayNameTextByValue(State.GroundMovementMode.GetValue()).ToString());
	}
	const FConnectionObservation& ReadConnection(const UConnectionObservationSubsystem* Service)
	{
		static const FConnectionObservation Empty;
		return Service ? Service->GetConnectionObservation() : Empty;
	}
	FString MetricText(const FObservedConnectionMetric& Metric, const TCHAR* Unit)
	{
		return Metric.IsAvailable() ? FString::Printf(TEXT("%.1f%s"), Metric.Value, Unit) : Unavailable;
	}

	FString Name(FName Value) { return Value.IsNone() ? Unavailable : Value.ToString(); }
	void SetText(UTextBlock* Widget, const FString& Value)
	{
		if (Widget) { Widget->SetText(FText::FromString(Value)); }
	}
}


UTextBlock* UCharacterObservationPanel::MakeText(FName Name, const FString& Text, int32 Size, bool bBold)
{
	UTextBlock* Widget = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Widget->SetText(FText::FromString(Text));
	Widget->SetFont(FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size));
	Widget->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.95f, 1.0f)));
	Widget->SetAutoWrapText(true);
	if (!Name.IsNone()) { Values.Add(Name, Widget); }
	return Widget;
}

UButton* UCharacterObservationPanel::MakeButton(FName Name, const FString& Label)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	FButtonStyle Style;
	Style.SetNormal(FSlateColorBrush(FLinearColor(0.085f, 0.13f, 0.20f)));
	Style.SetHovered(FSlateColorBrush(FLinearColor(0.14f, 0.24f, 0.36f)));
	Style.SetPressed(FSlateColorBrush(FLinearColor(0.04f, 0.30f, 0.52f)));
	Style.SetNormalPadding(FMargin(12.0f, 8.0f));
	Style.SetPressedPadding(FMargin(12.0f, 8.0f));
	Button->SetStyle(Style);
	UTextBlock* Text = MakeText(Key(Name, TEXT("_Label")), Label, 14, true);
	Text->SetAutoWrapText(false);
	Text->SetJustification(ETextJustify::Center);
	Button->SetContent(Text);
	return Button;
}

UTextBlock* UCharacterObservationPanel::AddValue(UVerticalBox* Section, FName Name, const FString& Label, bool bStacked)
{
	UTextBlock* Heading = MakeText(Key(Name, TEXT("_Label")), Label, 13);
	Heading->SetColorAndOpacity(FSlateColor(Muted));
	UTextBlock* Value = MakeText(Name, Unavailable, 14);
	Value->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
	Value->SetJustification(ETextJustify::Right);
	if (bStacked)
	{
		UVerticalBox* Row = WidgetTree->ConstructWidget<UVerticalBox>();
		Section->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.0f, 3.0f));
		Row->AddChildToVerticalBox(Heading);
		Row->AddChildToVerticalBox(Value);
	}
	else
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		Section->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.0f, 3.0f));
		Row->AddChildToHorizontalBox(Heading)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Row->AddChildToHorizontalBox(Value)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	return Value;
}

UEditableTextBox* UCharacterObservationPanel::AddInput(UVerticalBox* Section, FName Name, const FString& Label, const FString& InitialValue)
{
	UVerticalBox* Field = WidgetTree->ConstructWidget<UVerticalBox>();
	Section->AddChildToVerticalBox(Field)->SetPadding(FMargin(0.0f, 5.0f));
	Field->AddChildToVerticalBox(MakeText(NAME_None, Label, 13))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 3.0f));
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
	Field->AddChildToVerticalBox(Row);
	UEditableTextBox* Input = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), Name);
	Input->SetText(FText::FromString(InitialValue));
	FEditableTextBoxStyle Style = Input->GetWidgetStyle();
	Style.SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 14));
	Style.SetBackgroundImageNormal(FSlateColorBrush(FLinearColor(0.075f, 0.10f, 0.14f)));
	Style.SetBackgroundImageHovered(FSlateColorBrush(FLinearColor(0.10f, 0.16f, 0.23f)));
	Style.SetBackgroundImageFocused(FSlateColorBrush(FLinearColor(0.06f, 0.20f, 0.32f)));
	Style.SetPadding(FMargin(8.0f, 5.0f));
	Style.SetForegroundColor(FLinearColor::White);
	Input->SetWidgetStyle(Style);
	Input->SetMinDesiredWidth(92.0f);
	Input->SetSelectAllTextWhenFocused(true);
	USizeBox* InputSize = WidgetTree->ConstructWidget<USizeBox>();
	InputSize->SetWidthOverride(112.0f);
	InputSize->SetContent(Input);
	Row->AddChildToHorizontalBox(InputSize)->SetVerticalAlignment(VAlign_Center);
	return Input;
}

UComboBoxString* UCharacterObservationPanel::MakeComboBox(FName Name)
{
	UComboBoxString* Combo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), Name);
	FComboBoxStyle ComboStyle = Combo->GetWidgetStyle();
	FButtonStyle& ButtonStyle = ComboStyle.ComboButtonStyle.ButtonStyle;
	ButtonStyle.SetNormal(FSlateColorBrush(FLinearColor(0.075f, 0.10f, 0.14f)));
	ButtonStyle.SetHovered(FSlateColorBrush(FLinearColor(0.10f, 0.16f, 0.23f)));
	ButtonStyle.SetPressed(FSlateColorBrush(FLinearColor(0.06f, 0.20f, 0.32f)));
	ButtonStyle.SetNormalForeground(FLinearColor::White);
	ButtonStyle.SetHoveredForeground(FLinearColor::White);
	ButtonStyle.SetPressedForeground(FLinearColor::White);
	ComboStyle.ComboButtonStyle.SetMenuBorderBrush(FSlateColorBrush(CardSurface));
	ComboStyle.ComboButtonStyle.DownArrowImage.TintColor = FSlateColor(FLinearColor::White);
	Combo->SetWidgetStyle(ComboStyle);
	Combo->SetContentPadding(FMargin(8.0f, 6.0f));
	FTableRowStyle ItemStyle = Combo->GetItemStyle();
	const FSlateColorBrush Hovered(FLinearColor(0.10f, 0.16f, 0.23f));
	const FSlateColorBrush Selected(FLinearColor(0.06f, 0.20f, 0.32f));
	ItemStyle.SetEvenRowBackgroundBrush(FSlateColorBrush(CardSurface));
	ItemStyle.SetOddRowBackgroundBrush(FSlateColorBrush(CardSurface));
	ItemStyle.SetEvenRowBackgroundHoveredBrush(Hovered);
	ItemStyle.SetOddRowBackgroundHoveredBrush(Hovered);
	ItemStyle.SetActiveBrush(Selected);
	ItemStyle.SetActiveHoveredBrush(Selected);
	ItemStyle.SetInactiveBrush(Selected);
	ItemStyle.SetInactiveHoveredBrush(Selected);
	ItemStyle.SetTextColor(FLinearColor::White);
	ItemStyle.SetSelectedTextColor(FLinearColor::White);
	Combo->SetItemStyle(ItemStyle);
	Combo->OnGenerateWidgetEvent.BindDynamic(this, &ThisClass::GenerateComboOption);
	Combo->SetMaxListHeight(240.0f);
	return Combo;
}

UBorder* UCharacterObservationPanel::MakeStateCard(FName Prefix, const FString& Title, const FLinearColor& Color)
{
	UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Key(Prefix, TEXT("_Card")));
	Border->SetBrushColor(CardSurface);
	Border->SetPadding(FMargin(14.0f));
	UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>();
	Border->SetContent(Body);
	UTextBlock* Heading = MakeText(Key(Prefix, TEXT("_Title")), Title, 17, true);
	Heading->SetColorAndOpacity(FSlateColor(Color));
	Body->AddChildToVerticalBox(Heading)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	UTextBlock* Availability = MakeText(Key(Prefix, TEXT("_Availability")), Unavailable, 12);
	Availability->SetColorAndOpacity(FSlateColor(Muted));
	Body->AddChildToVerticalBox(Availability)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	AddValue(Body, Key(Prefix, TEXT("_Position")), TEXT("Position XYZ (zero origin cm)"), true);
	AddValue(Body, Key(Prefix, TEXT("_PositionX")), TEXT("Position X (cm)"));
	AddValue(Body, Key(Prefix, TEXT("_PositionY")), TEXT("Position Y (cm)"));
	AddValue(Body, Key(Prefix, TEXT("_PositionZ")), TEXT("Position Z (cm)"));
	AddValue(Body, Key(Prefix, TEXT("_Velocity")), TEXT("Velocity XYZ (cm/s)"), true);
	AddValue(Body, Key(Prefix, TEXT("_VelocityX")), TEXT("Velocity X (cm/s)"));
	AddValue(Body, Key(Prefix, TEXT("_VelocityY")), TEXT("Velocity Y (cm/s)"));
	AddValue(Body, Key(Prefix, TEXT("_VelocityZ")), TEXT("Velocity Z (cm/s)"));
	AddValue(Body, Key(Prefix, TEXT("_Facing")), TEXT("Rotation P / Y / R (deg)"));
	AddValue(Body, Key(Prefix, TEXT("_Movement")), TEXT("Movement"));
	AddValue(Body, Key(Prefix, TEXT("_Action")), TEXT("Actual crouch"));
	AddValue(Body, Key(Prefix, TEXT("_Time")), TEXT("Time (s)"));
	return Border;
}

void UCharacterObservationPanel::BuildInterface()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
	ReadabilityRoot = WidgetTree->ConstructWidget<UScaleBox>();
	ReadabilityRoot->SetStretch(EStretch::UserSpecified);
	ReadabilityRoot->SetContent(Root);
	// Type contract (caller guaranteed): SetContent above installs Root in ReadabilityRoot's UScaleBoxSlot.
	CastChecked<UScaleBoxSlot>(Root->Slot)->SetHorizontalAlignment(HAlign_Fill);
	CastChecked<UScaleBoxSlot>(Root->Slot)->SetVerticalAlignment(VAlign_Fill);
	WidgetTree->RootWidget = ReadabilityRoot;
	HudSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ObservationHUD"));
	UCanvasPanelSlot* HudSlot = Root->AddChildToCanvas(HudSize);
	HudSlot->SetAnchors(FAnchors(1.0f, 0.0f));
	HudSlot->SetAlignment(FVector2D(1.0, 0.0));
	HudSlot->SetPosition(FVector2D(-16.0, 16.0));
	HudSlot->SetAutoSize(true);
	HudSize->SetVisibility(ESlateVisibility::HitTestInvisible);
	UBorder* HudBorder = WidgetTree->ConstructWidget<UBorder>();
	HudBorder->SetBrushColor(Surface);
	HudBorder->SetPadding(FMargin(14.0f));
	HudSize->SetContent(HudBorder);
	UVerticalBox* Hud = WidgetTree->ConstructWidget<UVerticalBox>();
	HudBorder->SetContent(Hud);
	Hud->AddChildToVerticalBox(MakeText(TEXT("HUD_Identity"), TEXT("Waiting for a possessed pawn"), 13));
	Hud->AddChildToVerticalBox(MakeText(TEXT("HUD_RTT"), TEXT("RTT \u2014"), 22, true))->SetPadding(FMargin(0.0f, 6.0f));
	Hud->AddChildToVerticalBox(MakeText(TEXT("HUD_Moves"), TEXT("Unacked \u2014"), 13));
	Hud->AddChildToVerticalBox(MakeText(TEXT("HUD_Corrections"), TEXT("Corrections \u2014"), 13));
	Hud->AddChildToVerticalBox(MakeText(TEXT("HUD_Scenario"), TEXT("Scenario: None"), 13))->SetPadding(FMargin(0.0f, 4.0f));
	UTextBlock* Hint = MakeText(TEXT("HUD_Hint"), TEXT("F2  \u00b7  Details"), 13, true);
	Hint->SetColorAndOpacity(FSlateColor(PredictionColor));
	Hud->AddChildToVerticalBox(Hint);

	DetailSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ObservationDetails"));
	UCanvasPanelSlot* DetailSlot = Root->AddChildToCanvas(DetailSize);
	DetailSlot->SetAnchors(FAnchors(1.0f, 0.0f));
	DetailSlot->SetAlignment(FVector2D(1.0, 0.0));
	DetailSlot->SetPosition(FVector2D(-16.0, 16.0));
	DetailSlot->SetAutoSize(true);
	DetailSize->SetVisibility(ESlateVisibility::Collapsed);
	UBorder* DetailBorder = WidgetTree->ConstructWidget<UBorder>();
	DetailBorder->SetBrushColor(Surface);
	DetailBorder->SetPadding(FMargin(16.0f));
	DetailSize->SetContent(DetailBorder);
	UVerticalBox* Detail = WidgetTree->ConstructWidget<UVerticalBox>();
	DetailBorder->SetContent(Detail);

	UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>();
	UTextBlock* Title = MakeText(TEXT("Title"), TEXT("Observatory"), 20, true);
	Header->AddChildToHorizontalBox(Title)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	UButton* Close = MakeButton(TEXT("CloseButton"), TEXT("Close \u00b7 F2"));
	Close->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleClose);
	Header->AddChildToHorizontalBox(Close);
	Detail->AddChildToVerticalBox(Header);
	TargetSelector = MakeComboBox(TEXT("TargetSelector"));
	TargetSelector->AddOption(TEXT("Self \u00b7 Follow controlled pawn"));
	TargetSelector->SetSelectedIndex(0);
	TargetPawns.Add(nullptr);
	TargetSelector->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleTargetSelectionChanged);
	TargetSelector->SetToolTipText(FText::FromString(TEXT("Select a character to inspect. Ghosts for all observation characters remain live.")));
	Detail->AddChildToVerticalBox(TargetSelector)->SetPadding(FMargin(0.0f, 6.0f));
	Detail->AddChildToVerticalBox(MakeText(TEXT("Identity"), TEXT("Waiting for a possessed pawn"), 13))->SetPadding(FMargin(0.0f, 6.0f));
	Detail->AddChildToVerticalBox(MakeText(TEXT("LiveNetwork"), TEXT("No client connection"), 13))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	UTextBlock* InputHint = MakeText(TEXT("InputHint"), TEXT("Left Alt to edit \u00b7 F2 to close"), 11);
	InputHint->SetColorAndOpacity(FSlateColor(PredictionColor));
	Detail->AddChildToVerticalBox(InputHint)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));

	UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>();
	const TCHAR* Labels[] = {TEXT("State"), TEXT("Faults"), TEXT("Moves")};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UButton* Tab = MakeButton(FName(FString::Printf(TEXT("Tab%d"), Index)), Labels[Index]);
		TabButtons.Add(Tab);
		UHorizontalBoxSlot* TabSlot = Tabs->AddChildToHorizontalBox(Tab);
		TabSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TabSlot->SetPadding(FMargin(0.0f, 0.0f, Index < 2 ? 6.0f : 0.0f, 0.0f));
	}
	TabButtons[0]->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleStateTab);
	TabButtons[1]->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleFaultTab);
	TabButtons[2]->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleMovesTab);
	Detail->AddChildToVerticalBox(Tabs);

	UHorizontalBox* SnapshotBar = WidgetTree->ConstructWidget<UHorizontalBox>();
	FreezeButton = MakeButton(TEXT("FreezeButton"), TEXT("Freeze snapshot"));
	FreezeButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleFreeze);
	FreezeButton->SetToolTipText(FText::FromString(TEXT("Locks only state comparison and Saved moves. Gameplay, ghosts, network metrics and faults stay live.")));
	SnapshotBar->AddChildToHorizontalBox(FreezeButton);
	UHorizontalBoxSlot* SampleSlot = SnapshotBar->AddChildToHorizontalBox(MakeText(TEXT("SnapshotStatus"), TEXT("No observed pawn"), 13));
	SampleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	SampleSlot->SetVerticalAlignment(VAlign_Center);
	SampleSlot->SetPadding(FMargin(12.0f, 0.0f));
	Detail->AddChildToVerticalBox(SnapshotBar)->SetPadding(FMargin(0.0f, 10.0f));

	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>();
	Detail->AddChildToVerticalBox(Pages)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TArray<UVerticalBox*> Bodies;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>();
		Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);
		Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::Always);
		Scroll->SetClipping(EWidgetClipping::ClipToBounds);
		UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>();
		Scroll->AddChild(Body);
		Pages->AddChild(Scroll);
		PageScrolls.Add(Scroll);
		Bodies.Add(Body);
	}
	UTextBlock* Explanation = MakeText(TEXT("GhostLegend"), TEXT("Ghosts: server red / prediction blue \u00b7 same acknowledged Move ID."), 13);
	Explanation->SetToolTipText(FText::FromString(TEXT("Autonomous ghosts compare the same acknowledged move. Simulated ghosts show the decoded reference and current unsmoothed capsule.")));
	Explanation->SetColorAndOpacity(FSlateColor(Muted));
	Bodies[0]->AddChildToVerticalBox(Explanation)->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 10.0f));
	StateCards = WidgetTree->ConstructWidget<UGridPanel>();
	Bodies[0]->AddChildToVerticalBox(StateCards);
	Cards.Add(MakeStateCard(TEXT("Server"), TEXT("Server \u00b7 Last ACK"), ServerColor));
	Cards.Add(MakeStateCard(TEXT("Prediction"), TEXT("Prediction \u00b7 Before response"), PredictionColor));
	Cards.Add(MakeStateCard(TEXT("Current"), TEXT("Current simulation"), FLinearColor(0.85f, 0.91f, 0.96f)));
	Cards.Add(MakeStateCard(TEXT("Render"), TEXT("Mesh presentation"), FLinearColor(0.85f, 0.91f, 0.96f)));
	for (int32 Index = 0; Index < Cards.Num(); ++Index) { StateCards->AddChildToGrid(Cards[Index], Index, 0)->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 8.0f)); }
	UVerticalBox* Stats = Bodies[0];
	PredictionStats = WidgetTree->ConstructWidget<UVerticalBox>();
	Stats->AddChildToVerticalBox(PredictionStats);
	PredictionStats->AddChildToVerticalBox(MakeText(NAME_None, TEXT("ACKs & corrections"), 17, true))->SetPadding(FMargin(0.0f, 12.0f));
	AddValue(PredictionStats, TEXT("GoodACKs"), TEXT("Accepted good ACKs"))->GetParent()->SetToolTipText(
		FText::FromString(TEXT("Accepted good ACKs in this observation context. Cleared when role, controller or packed RPC protocol changes, or native client prediction data is reset; native timestamp wrap does not clear it.")));
	AddValue(PredictionStats, TEXT("Corrections"), TEXT("Accepted corrections"))->GetParent()->SetToolTipText(
		FText::FromString(TEXT("Accepted corrections in this observation context. Cleared when role, controller or packed RPC protocol changes, or native client prediction data is reset; native timestamp wrap does not clear it.")));
	AddValue(PredictionStats, TEXT("LastResponse"), TEXT("Last accepted response"));
	AddValue(PredictionStats, TEXT("ServerSequence"), TEXT("Server sample sequence"));
	AddValue(PredictionStats, TEXT("ServerTime"), TEXT("Server world time (s)"));
	AddValue(PredictionStats, TEXT("Unacked"), TEXT("Unacknowledged saved moves"));
	AddValue(PredictionStats, TEXT("UnackedDuration"), TEXT("Accumulated move duration (ms)"));
	AddValue(PredictionStats, TEXT("PairedMoveId"), TEXT("Paired Move ID"));
	AddValue(PredictionStats, TEXT("PairedResponse"), TEXT("Pair response / received (local s)"));
	AddValue(PredictionStats, TEXT("PairDifference"), TEXT("Same move difference"));
	AddValue(PredictionStats, TEXT("LastCorrection"), TEXT("Last valid correction"));
	AddValue(Stats, TEXT("ConsumedInput"), TEXT("Last consumed world input"));
	AddValue(Stats, TEXT("JumpRequest"), TEXT("Jump request at sample"));
	Stats->AddChildToVerticalBox(MakeText(NAME_None, TEXT("Presentation & movement base"), 17, true))->SetPadding(FMargin(0.0f, 12.0f));
	AddValue(Stats, TEXT("ReferenceAge"), TEXT("Network reference age (ms)"));
	AddValue(Stats, TEXT("SmoothingMode"), TEXT("Smoothing mode"));
	AddValue(Stats, TEXT("VisualOffset"), TEXT("Mesh smoothing offset (cm)"));
	AddValue(Stats, TEXT("Smoothing"), TEXT("Smoothing"));
	AddValue(Stats, TEXT("MovementBase"), TEXT("Movement base"));

	BuildFaultPage(Bodies[1]);
	Bodies[2]->AddChildToVerticalBox(MakeText(NAME_None, TEXT("Native unacknowledged SavedMoves, oldest to newest. Ghosts compare the last paired acknowledged move."), 13))->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 10.0f));
	Bodies[2]->AddChildToVerticalBox(MakeText(TEXT("MovesSummary"), TEXT("Prediction data unavailable"), 16, true))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
	MovesList = WidgetTree->ConstructWidget<UVerticalBox>();
	Bodies[2]->AddChildToVerticalBox(MovesList);

	FaultActions = WidgetTree->ConstructWidget<UUniformGridPanel>();
	FaultActions->SetSlotPadding(FMargin(3.0f));
	ApplyButton = MakeButton(TEXT("FaultApplyButton"), TEXT("Apply base"));
	ScenarioStartButton = MakeButton(TEXT("ScenarioStartButton"), TEXT("Run scenario"));
	ScenarioStopButton = MakeButton(TEXT("ScenarioStopButton"), TEXT("Stop scenario"));
	ResetButton = MakeButton(TEXT("FaultResetButton"), TEXT("Restore original"));
	int32 Column = 0;
	for (UButton* Action : {ApplyButton.Get(), ScenarioStartButton.Get(), ScenarioStopButton.Get(), ResetButton.Get()})
	{
		FaultActions->AddChildToUniformGrid(Action, 0, Column++);
	}
	ApplyButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleApply);
	ScenarioStartButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleScenarioStart);
	ScenarioStopButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleScenarioStop);
	ResetButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleReset);
	ResetButton->SetToolTipText(FText::FromString(TEXT("Restores the native settings captured before the first application and ends the scenario.")));
	Detail->AddChildToVerticalBox(FaultActions)->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	UpdateLayout(FVector2D(960.0, 720.0));
	SelectPage(0);
}

void UCharacterObservationPanel::BuildFaultPage(UVerticalBox* Section)
{
	FaultStatus = MakeText(TEXT("FaultStatus"), TEXT("No client connection"), 14, true);
	Section->AddChildToVerticalBox(FaultStatus)->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 8.0f));
	Section->AddChildToVerticalBox(MakeText(TEXT("FaultScopeHint"), TEXT("Settings affect this client's game connection. Local players sharing that connection share faults."), 13))->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 12.0f));
	FaultStatus->SetToolTipText(FText::FromString(TEXT("Base settings affect this client's connection, including local players sharing it. Nominal latency excludes scenario delay, tick delay and buffer bloat.")));
	Section->AddChildToVerticalBox(MakeText(NAME_None, TEXT("Base latency & loss"), 17, true));
	Section->AddChildToVerticalBox(MakeText(TEXT("BaseDraft"), TEXT("Draft matches applied settings"), 12))->SetPadding(FMargin(0.0f, 4.0f));
	UTextBlock* Columns = MakeText(NAME_None, TEXT("Editable draft  /  Applied value"), 12);
	Columns->SetJustification(ETextJustify::Right);
	Columns->SetColorAndOpacity(FSlateColor(Muted));
	Section->AddChildToVerticalBox(Columns);
	AdvancedEditors = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("AdvancedEditors"));
	for (bool bIncoming : {false, true})
	{
		AdvancedEditors->AddChildToVerticalBox(MakeText(NAME_None, bIncoming ? TEXT("Incoming") : TEXT("Outgoing"), 15, true))->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 6.0f));
		for (const FFaultField& Field : FaultFields)
		{
			const bool bFieldIncoming = FString(Field.Name).StartsWith(TEXT("PktIncoming"));
			if (bFieldIncoming != bIncoming) { continue; }
			UVerticalBox* Container = IsCommon(Field.Name) ? Section : AdvancedEditors.Get();
			UEditableTextBox* Input = AddInput(Container, FName(FString(TEXT("FAULT_")) + Field.Name), Field.Label, TEXT("0"));
			const FText Hint = FText::FromString(FString::Printf(TEXT("%s: %s"), Field.Name, Field.Hint));
			Input->SetToolTipText(Hint);
			Input->GetParent()->GetParent()->GetParent()->SetToolTipText(Hint);
			Input->OnTextChanged.AddUniqueDynamic(this, &ThisClass::HandleFaultInputChanged);
			FaultInputs.Add(Field.Name, Input);
			// Type contract (caller guaranteed): AddInput builds EditableTextBox -> SizeBox -> HorizontalBox.
			// Keep that native builder hierarchy when changing the fault editor layout.
			UHorizontalBox* Row = CastChecked<UHorizontalBox>(Input->GetParent()->GetParent());
			USizeBox* ActualSize = WidgetTree->ConstructWidget<USizeBox>();
			ActualSize->SetWidthOverride(80.0f);
			UTextBlock* Actual = MakeText(Key(Field.Name, TEXT("_Applied")), Unavailable, 14);
			Actual->SetJustification(ETextJustify::Right);
			Actual->SetColorAndOpacity(FSlateColor(Muted));
			ActualSize->SetContent(Actual);
			UHorizontalBoxSlot* ActualSlot = Row->AddChildToHorizontalBox(ActualSize);
			ActualSlot->SetVerticalAlignment(VAlign_Center);
			ActualSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	}
	RevertButton = MakeButton(TEXT("RevertBaseButton"), TEXT("Revert base draft"));
	RevertButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRevert);
	Section->AddChildToVerticalBox(RevertButton)->SetPadding(FMargin(0.0f, 8.0f));
	FaultMessage = MakeText(TEXT("FaultMessage"), TEXT(""), 13);
	FaultMessage->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.76f, 0.38f)));
	Section->AddChildToVerticalBox(FaultMessage)->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 10.0f));
	CreateScenarioControls(Section);
	UButton* Advanced = MakeButton(TEXT("AdvancedButton"), TEXT("Show advanced base settings"));
	Advanced->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleAdvanced);
	Section->AddChildToVerticalBox(Advanced)->SetPadding(FMargin(0.0f, 12.0f));
	AdvancedEditors->SetVisibility(ESlateVisibility::Collapsed);
	Section->AddChildToVerticalBox(AdvancedEditors);
}

void UCharacterObservationPanel::CreateScenarioControls(UVerticalBox* Section)
{
	Section->AddChildToVerticalBox(MakeText(NAME_None, TEXT("Outgoing fault scenario"), 17, true))->SetPadding(FMargin(0.0f, 12.0f, 0.0f, 8.0f));
	ScenarioStatus = MakeText(TEXT("ScenarioStatus"), TEXT("None"), 14);
	Section->AddChildToVerticalBox(ScenarioStatus)->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 6.0f));
	Section->AddChildToVerticalBox(MakeText(TEXT("ScenarioApplied"), TEXT(""), 13))->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 6.0f));
	Section->AddChildToVerticalBox(MakeText(TEXT("ScenarioDraft"), TEXT("Draft matches configured scenario"), 12))->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	ScenarioSelector = MakeComboBox(TEXT("ScenarioSelector"));
	for (const TCHAR* Name : ScenarioNames) { ScenarioSelector->AddOption(Name); }
	ScenarioSelector->SetSelectedIndex(0);
	ScenarioSelector->OnSelectionChanged.AddUniqueDynamic(this, &ThisClass::HandleScenarioSelectionChanged);
	Section->AddChildToVerticalBox(ScenarioSelector)->SetPadding(FMargin(0.0f, 6.0f));
	UHorizontalBox* RepeatRow = WidgetTree->ConstructWidget<UHorizontalBox>();
	ScenarioRepeat = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass(), TEXT("ScenarioRepeat"));
	ScenarioRepeat->OnCheckStateChanged.AddUniqueDynamic(this, &ThisClass::HandleScenarioRepeatChanged);
	RepeatRow->AddChildToHorizontalBox(ScenarioRepeat);
	UHorizontalBoxSlot* RepeatSlot = RepeatRow->AddChildToHorizontalBox(MakeText(NAME_None, TEXT("Repeat \u00b7 first cycle immediately"), 13));
	RepeatSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	RepeatSlot->SetVerticalAlignment(VAlign_Center);
	Section->AddChildToVerticalBox(RepeatRow)->SetPadding(FMargin(0.0f, 6.0f));
	const FNetworkFaultScenarioSettings Defaults;
	UEditableTextBox* Burst = AddInput(Section, TEXT("SCENARIO_BurstPacketCount"), TEXT("Consecutive dropped packets"), FString::FromInt(Defaults.BurstPacketCount));
	ScenarioInputs.Add(TEXT("BurstPacketCount"), Burst);
	Burst->OnTextChanged.AddUniqueDynamic(this, &ThisClass::HandleScenarioInputChanged);
	for (const FScenarioField& Field : ScenarioFields)
	{
		UEditableTextBox* Input = AddInput(Section, FName(FString(TEXT("SCENARIO_")) + Field.Name), Field.Label, FString::SanitizeFloat(Defaults.*Field.Member));
		Input->OnTextChanged.AddUniqueDynamic(this, &ThisClass::HandleScenarioInputChanged);
		ScenarioInputs.Add(Field.Name, Input);
	}
	ScenarioInputs.FindChecked(TEXT("Bandwidth"))->SetToolTipText(FText::FromString(TEXT("1000 bits per second per Kbps; excludes later protocol and IP/UDP overhead. Continuous until stopped.")));
	ScenarioRevertButton = MakeButton(TEXT("RevertScenarioButton"), TEXT("Revert scenario draft"));
	ScenarioRevertButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleScenarioRevert);
	Section->AddChildToVerticalBox(ScenarioRevertButton)->SetPadding(FMargin(0.0f, 8.0f));
	ScenarioMessage = MakeText(TEXT("ScenarioMessage"), TEXT(""), 13);
	ScenarioMessage->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.76f, 0.38f)));
	Section->AddChildToVerticalBox(ScenarioMessage)->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	UpdateScenarioControls();
}

UWidget* UCharacterObservationPanel::GenerateComboOption(FString Option)
{
	UTextBlock* Text = MakeText(NAME_None, Option);
	Text->SetAutoWrapText(false);
	Text->SetClipping(EWidgetClipping::ClipToBounds);
	Text->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
	Text->SetToolTipText(FText::FromString(Option));
	return Text;
}

void UCharacterObservationPanel::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	BuildInterface();
}

void UCharacterObservationPanel::NativeConstruct()
{
	Super::NativeConstruct();
	FSlateApplication::Get().OnApplicationActivationStateChanged().AddWeakLambda(this, [this](bool bActive)
	{
		if (!bActive)
		{
			if (auto Controller = Cast<AObservationPlayerController>(GetOwningPlayer())) { Controller->SetObservationUIInput(false); }
		}
	});
	ClearSnapshot();
	RefreshBindings();
	SetExpanded(false);
}

void UCharacterObservationPanel::NativeDestruct()
{
	if (FSlateApplication::IsInitialized()) { FSlateApplication::Get().OnApplicationActivationStateChanged().RemoveAll(this); }
	if (AObservationPlayerController* Controller = Cast<AObservationPlayerController>(GetOwningPlayer()))
	{
		Controller->SetObservationUIInput(false);
	}
	if (ObservationSubsystem.IsValid()) { ObservationSubsystem->OnPawnObservationSnapshotUpdated.RemoveDynamic(this, &ThisClass::HandleSnapshotUpdated); }
	if (NetworkFaultSubsystem.IsValid()) { NetworkFaultSubsystem->OnSimulationStateChanged.RemoveDynamic(this, &ThisClass::HandleFaultStateChanged); }
	ObservationSubsystem.Reset();
	NetworkFaultSubsystem.Reset();
	ConnectionSubsystem.Reset();
	InputConnection.Reset();
	bFrozen = false;
	Super::NativeDestruct();
}

void UCharacterObservationPanel::RefreshBindings()
{
	ULocalPlayer* Player = GetOwningLocalPlayer();
	ULocalPlayerObservationSubsystem* Observation = Player ? Player->GetSubsystem<ULocalPlayerObservationSubsystem>() : nullptr;
	UNetworkFaultSimulationSubsystem* Faults = GetWorld() ? GetWorld()->GetSubsystem<UNetworkFaultSimulationSubsystem>() : nullptr;
	ConnectionSubsystem = GetWorld() ? GetWorld()->GetSubsystem<UConnectionObservationSubsystem>() : nullptr;
	if (ObservationSubsystem.Get() != Observation)
	{
		if (ObservationSubsystem.IsValid()) { ObservationSubsystem->OnPawnObservationSnapshotUpdated.RemoveDynamic(this, &ThisClass::HandleSnapshotUpdated); }
		ObservationSubsystem = Observation;
		ClearSnapshot();
		if (Observation)
		{
			Observation->OnPawnObservationSnapshotUpdated.AddUniqueDynamic(this, &ThisClass::HandleSnapshotUpdated);
			if (Observation->GetPawnObservationSnapshot().IsValid()) { HandleSnapshotUpdated(Observation->GetPawnObservationSnapshot()); }
		}
	}
	if (NetworkFaultSubsystem.Get() != Faults)
	{
		if (NetworkFaultSubsystem.IsValid()) { NetworkFaultSubsystem->OnSimulationStateChanged.RemoveDynamic(this, &ThisClass::HandleFaultStateChanged); }
		NetworkFaultSubsystem = Faults;
		if (Faults) { Faults->OnSimulationStateChanged.AddUniqueDynamic(this, &ThisClass::HandleFaultStateChanged); }
		InputConnection.Reset();
		bFaultInputsDirty = false;
		bScenarioInputsDirty = false;
		HandleFaultStateChanged();
	}
}

void UCharacterObservationPanel::NativeTick(const FGeometry& MyGeometry, float DeltaTime)
{
	Super::NativeTick(MyGeometry, DeltaTime);
	// Slate keeps ticking while the world is paused and while a text field owns keyboard input.
	if (AObservationPlayerController* Controller = Cast<AObservationPlayerController>(GetOwningPlayer()))
	{
		Controller->UpdateObservationUIInput();
	}
	RefreshBindings();
	if (bDisplayingSnapshot && (!ObservationSubsystem.IsValid() || !ObservationSubsystem->GetPawnObservationSnapshot().IsValid())) { ClearSnapshot(); }
	// Keep readable text in small PIE windows while retaining larger DPI scales and owning-player bounds.
	const float DPIScale = UWidgetLayoutLibrary::GetViewportScale(this);
	const float ReadabilityScale = FMath::Max(1.0f, 1.0f / FMath::Max(DPIScale, UE_SMALL_NUMBER));
	ReadabilityRoot->SetUserSpecifiedScale(ReadabilityScale);
	const FVector2D Size = MyGeometry.GetLocalSize() / ReadabilityScale;
	if (Size.X > 0.0 && Size.Y > 0.0 && !LastViewportSize.Equals(Size)) { UpdateLayout(Size); }
	const TWeakObjectPtr<UNetConnection> Connection = ConnectionSubsystem.IsValid() ? ConnectionSubsystem->GetConnectionObservation().Connection : TWeakObjectPtr<UNetConnection>{};
	if (InputConnection != Connection) { HandleFaultStateChanged(); }
	RefreshElapsed += DeltaTime;
	if (RefreshElapsed >= 0.1f)
	{
		RefreshElapsed = 0.0f;
		if (bExpanded) { RefreshTargets(); }
		RefreshDisplay();
		HandleFaultStateChanged();
	}
}

void UCharacterObservationPanel::UpdateLayout(const FVector2D& Size)
{
	LastViewportSize = Size;
	HudSize->SetWidthOverride(FMath::Max(160.0, FMath::Min(360.0, Size.X - 32.0)));
	const double Width = FMath::Min(560.0, Size.X * 0.45);
	DetailSize->SetWidthOverride(Width);
	DetailSize->SetHeightOverride(FMath::Max(0.0, Size.Y - 32.0));
	const bool bCompact = Width < 380.0 || Size.Y < 620.0;
	// Type contract (caller guaranteed): BuildInterface installs a UBorder as DetailSize's content.
	CastChecked<UBorder>(DetailSize->GetContent())->SetPadding(FMargin(bCompact ? 12.0f : 16.0f));
	Values.FindChecked(TEXT("Title"))->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), bCompact ? 17 : 20));
	Values.FindChecked(TEXT("Identity"))->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), bCompact ? 11 : 13));
	Values.FindChecked(TEXT("LiveNetwork"))->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), bCompact ? 11 : 13));
	Values.FindChecked(TEXT("SnapshotStatus"))->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), bCompact ? 11 : 13));
	// Type contract (caller guaranteed): BuildInterface adds Identity, LiveNetwork and the freeze row
	// directly to a UVerticalBox; each keeps its UVerticalBoxSlot when the layout is updated.
	CastChecked<UVerticalBoxSlot>(Values.FindChecked(TEXT("Identity"))->Slot)->SetPadding(FMargin(0.0f, bCompact ? 2.0f : 6.0f));
	CastChecked<UVerticalBoxSlot>(Values.FindChecked(TEXT("LiveNetwork"))->Slot)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, bCompact ? 4.0f : 10.0f));
	CastChecked<UVerticalBoxSlot>(FreezeButton->GetParent()->Slot)->SetPadding(FMargin(0.0f, bCompact ? 4.0f : 10.0f));
	Values.FindChecked(TEXT("FaultScopeHint"))->SetVisibility(bCompact ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	auto SetButtonDensity = [&](UButton* Button)
	{
		FButtonStyle Style = Button->GetStyle();
		Style.SetNormalPadding(FMargin(bCompact ? 6.0f : 12.0f, bCompact ? 5.0f : 8.0f));
		Style.SetPressedPadding(Style.NormalPadding);
		Button->SetStyle(Style);
		Values.FindChecked(Key(Button->GetFName(), TEXT("_Label")))->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), bCompact ? 12 : 14));
	};
	// Type contract (caller guaranteed): BuildInterface creates the named CloseButton via MakeButton.
	SetButtonDensity(CastChecked<UButton>(GetWidgetFromName(TEXT("CloseButton"))));
	SetButtonDensity(FreezeButton);
	SetButtonDensity(RevertButton);
	SetButtonDensity(ScenarioRevertButton);
	// Type contract (caller guaranteed): BuildFaultPage creates the named AdvancedButton via MakeButton.
	SetButtonDensity(CastChecked<UButton>(GetWidgetFromName(TEXT("AdvancedButton"))));
	for (UButton* Tab : TabButtons) { SetButtonDensity(Tab); }
	StateCards->SetColumnFill(0, 1.0f);
	for (FName Prefix : {FName(TEXT("Server")), FName(TEXT("Prediction")), FName(TEXT("Current")), FName(TEXT("Render"))})
	{
		for (const TCHAR* Suffix : {TEXT("_Position"), TEXT("_Velocity")})
		{
			const bool bHiddenVelocity = FCString::Strcmp(Suffix, TEXT("_Velocity")) == 0
				&& (Prefix == TEXT("Render") || (Prefix == TEXT("Server") && DisplaySnapshot.GetRole() == EObservedPawnRole::SimulatedProxy));
			Values.FindChecked(Key(Prefix, Suffix))->GetParent()->SetVisibility(bHiddenVelocity ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		}
		for (const TCHAR* Suffix : {TEXT("_PositionX"), TEXT("_PositionY"), TEXT("_PositionZ"), TEXT("_VelocityX"), TEXT("_VelocityY"), TEXT("_VelocityZ")})
		{
			Values.FindChecked(Key(Prefix, Suffix))->GetParent()->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	int32 Index = 0;
	for (UButton* Action : {ApplyButton.Get(), ScenarioStartButton.Get(), ScenarioStopButton.Get(), ResetButton.Get()})
	{
		SetButtonDensity(Action);
		// Type contract (caller guaranteed): BuildInterface adds each action button to FaultActions' UUniformGridPanel.
		UUniformGridSlot* ActionSlot = CastChecked<UUniformGridSlot>(Action->Slot);
		ActionSlot->SetRow(Index / 2);
		ActionSlot->SetColumn(Index % 2);
		++Index;
	}
}

void UCharacterObservationPanel::SetExpanded(bool bInExpanded)
{
	bExpanded = bInExpanded;
	if (!bInExpanded) { bUIInputEnabled = false; }
	SetUIInputEnabled(bUIInputEnabled);
	HudSize->SetVisibility(bInExpanded ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	if (!bInExpanded) { bFrozen = false; bSnapshotPending = true; }
	else { RefreshTargets(); }
	RefreshDisplay();
	HandleFaultStateChanged();
}

void UCharacterObservationPanel::SetUIInputEnabled(bool bEnabled)
{
	bUIInputEnabled = bExpanded && bEnabled;
	DetailSize->SetVisibility(!bExpanded ? ESlateVisibility::Collapsed
		: (bUIInputEnabled ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible));
	SetValue(TEXT("InputHint"), bUIInputEnabled ? TEXT("Left Alt to play \u00b7 F2 to close") : TEXT("Left Alt to edit \u00b7 F2 to close"));
}

void UCharacterObservationPanel::HandleClose()
{
	if (AObservationPlayerController* Controller = Cast<AObservationPlayerController>(GetOwningPlayer()))
	{
		if (Controller->IsObservationPanelExpanded()) { Controller->ToggleObservationPanelExpansion(); }
	}
}

void UCharacterObservationPanel::SelectPage(int32 Index)
{
	if (Index == 2 && LiveSnapshot.GetRole() != EObservedPawnRole::AutonomousProxy) { Index = 0; }
	Pages->SetActiveWidgetIndex(Index);
	for (int32 Tab = 0; Tab < TabButtons.Num(); ++Tab)
	{
		TabButtons[Tab]->SetBackgroundColor(Tab == Index ? FLinearColor(0.30f, 0.70f, 1.0f) : FLinearColor::White);
	}
	FaultActions->SetVisibility(Index == 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	RefreshDisplay();
	if (Index == 2) { RenderMoves(); }
	HandleFaultStateChanged();
}

void UCharacterObservationPanel::HandleStateTab() { SelectPage(0); }
void UCharacterObservationPanel::HandleFaultTab() { SelectPage(1); }
void UCharacterObservationPanel::HandleMovesTab() { SelectPage(2); }

void UCharacterObservationPanel::HandleFreeze()
{
	if (!bDisplayingSnapshot || !bExpanded) { return; }
	bFrozen = !bFrozen;
	if (bFrozen) { DisplaySnapshot = LiveSnapshot; }
	bSnapshotPending = true;
	RefreshDisplay();
}

void UCharacterObservationPanel::HandleSnapshotUpdated(const FPawnObservationSnapshot& Snapshot)
{
	const FPawnObservationFrame* Old = LiveSnapshot.Get();
	const FPawnObservationFrame* New = Snapshot.Get();
	const bool bChangedTarget = !Old || !New || Old->GetCommon().Pawn != New->GetCommon().Pawn
		|| Old->GetCommon().World != New->GetCommon().World || Old->GetRole() != New->GetRole()
		|| Old->GetSourceEpoch() != New->GetSourceEpoch();
	LiveSnapshot = Snapshot;
	bDisplayingSnapshot = Snapshot.IsValid();
	if (bChangedTarget) { bFrozen = false; }
	if (Pages->GetActiveWidgetIndex() == 2 && Snapshot.GetRole() != EObservedPawnRole::AutonomousProxy) { SelectPage(0); }
	if (!bFrozen) { bSnapshotPending = true; }
	if (bChangedTarget) { RefreshDisplay(); }
}

void UCharacterObservationPanel::ClearSnapshot()
{
	LiveSnapshot = {};
	bDisplayingSnapshot = false;
	bFrozen = false;
	bSnapshotPending = true;
	if (Pages->GetActiveWidgetIndex() == 2) { SelectPage(0); }
	RefreshDisplay();
}

void UCharacterObservationPanel::SetValue(FName Name, const FString& Text)
{
	if (UTextBlock* Widget = Values.FindRef(Name); Widget && Widget->GetText().ToString() != Text)
	{
		Widget->SetText(FText::FromString(Text));
	}
}

void UCharacterObservationPanel::RefreshDisplay()
{
	if (!bFrozen) { DisplaySnapshot = LiveSnapshot; }
	RefreshLiveSummary();
	SetValue(TEXT("FreezeButton_Label"), bFrozen ? TEXT("Resume live") : TEXT("Freeze snapshot"));
	FreezeButton->SetIsEnabled(bDisplayingSnapshot);
	SetValue(TEXT("SnapshotStatus"), bDisplayingSnapshot
		? FString::Printf(TEXT("%s \u00b7 sample %.3f s"), bFrozen ? TEXT("FROZEN") : TEXT("LIVE"), DisplaySnapshot.Get()->GetCommon().SampleTime.GameTimeSeconds)
		: TEXT("No observed pawn"));
	if (bSnapshotPending)
	{
		RenderSnapshot();
		if (Pages->GetActiveWidgetIndex() == 2) { RenderMoves(); }
		bSnapshotPending = false;
	}
}

void UCharacterObservationPanel::RefreshLiveSummary()
{
	const FConnectionObservation EmptyConnection;
	const FConnectionObservation& Network = ConnectionSubsystem.IsValid() ? ConnectionSubsystem->GetConnectionObservation() : EmptyConnection;
	const FPawnObservationFrame* Frame = LiveSnapshot.Get();
	const FPawnObservationCommon EmptyCommon;
	const FPawnObservationCommon& Common = Frame ? Frame->GetCommon() : EmptyCommon;
	const FString PlayerId = Common.OwningPlayerId == INDEX_NONE ? Unavailable : FString::FromInt(Common.OwningPlayerId);
	const FString Identity = Frame ? FString::Printf(TEXT("%s \u00b7 %s \u00b7 Player %s"),
		*Common.PawnName, RoleName(Frame->GetRole()), *PlayerId) : TEXT("Waiting for an observed pawn");
	SetValue(TEXT("HUD_Identity"), Identity);
	SetValue(TEXT("Identity"), Identity);
	SetValue(TEXT("HUD_RTT"), FString(TEXT("RTT ")) + MetricText(Network.RoundTripTimeMs, TEXT(" ms")));
	SetValue(TEXT("LiveNetwork"), Network.Status == EObservationAvailability::Available
		? FString::Printf(TEXT("LIVE \u00b7 RTT %s \u00b7 send loss %s\nJitter estimate %s \u00b7 native sample time unknown"),
			*MetricText(Network.RoundTripTimeMs, TEXT(" ms")), *MetricText(Network.OutgoingLossPercent, TEXT("%")),
			*MetricText(Network.JitterEstimateMs, TEXT(" ms")))
		: TEXT("No open client game connection"));
	if (const FAutonomousPawnObservation* Auto = Frame ? Frame->GetAutonomous() : nullptr)
	{
		const bool bPrediction = Auto->PredictionStatus == EObservationAvailability::Available;
		SetValue(TEXT("HUD_Moves"), bPrediction ? FString::Printf(TEXT("Unacked %d moves \u00b7 %.1f ms"),
			Auto->UnacknowledgedMoves.Num(), Auto->GetUnacknowledgedDurationSeconds() * 1000.0) : Availability(Auto->PredictionStatus));
		SetValue(TEXT("HUD_Corrections"), Auto->NativeResponsesStatus == EObservationAvailability::Available ? FString::Printf(TEXT("Corrections %lld \u00b7 Good ACKs %lld"),
			Auto->AcceptedCorrectionCount, Auto->AcceptedGoodAckCount) : Availability(Auto->NativeResponsesStatus));
	}
	else if (const FSimulatedPawnObservation* Sim = Frame ? Frame->GetSimulated() : nullptr)
	{
		SetValue(TEXT("HUD_Moves"), Sim->LastDecodedReference.IsAvailable() ? FString::Printf(TEXT("Reference age %.1f ms"), ReferenceAgeMs(LiveSnapshot)) : Availability(Sim->LastDecodedReference.Status));
		SetValue(TEXT("HUD_Corrections"), Common.Smoothing.Status == EObservationAvailability::Available
			? FString::Printf(TEXT("Smoothing %s"), Common.Smoothing.bComplete ? TEXT("complete") : TEXT("active")) : Unavailable);
	}
	else
	{
		SetValue(TEXT("HUD_Moves"), Frame ? TEXT("Current local simulation") : TEXT("Waiting for an observed pawn"));
		SetValue(TEXT("HUD_Corrections"), Unavailable);
	}
}

void UCharacterObservationPanel::RefreshTargets()
{
	if (!ObservationSubsystem.IsValid()) { return; }
	TArray<AObservationCharacter*> Characters;
	for (TActorIterator<AObservationCharacter> It(GetWorld()); It; ++It) { if (IsValid(*It)) { Characters.Add(*It); } }
	Characters.Sort([](const AObservationCharacter& A, const AObservationCharacter& B) { return A.GetUniqueID() < B.GetUniqueID(); });
	TArray<TWeakObjectPtr<APawn>> NewTargets;
	TArray<FString> Options;
	NewTargets.Add(nullptr);
	Options.Add(TEXT("Self \u00b7 Follow controlled pawn"));
	for (AObservationCharacter* Character : Characters)
	{
		NewTargets.Add(Character);
		const APlayerState* Player = Character->GetPlayerState<APlayerState>();
		const FString PlayerId = IsValid(Player) ? FString::FromInt(Player->GetPlayerId()) : Unavailable;
		const TCHAR* Role = Character->GetLocalRole() == ROLE_AutonomousProxy ? TEXT("Autonomous Proxy")
			: (Character->GetLocalRole() == ROLE_SimulatedProxy ? TEXT("Simulated Proxy")
				: (Character->GetLocalRole() == ROLE_Authority ? TEXT("Authority") : TEXT("Unknown")));
		Options.Add(FString::Printf(TEXT("Player %s \u00b7 %s \u00b7 %s"), *PlayerId, Role, *Character->GetName()));
	}
	APawn* Selected = ObservationSubsystem->IsFollowingControlledPawn() ? nullptr : ObservationSubsystem->GetObservedPawn();
	const int32 Index = NewTargets.IndexOfByPredicate([Selected](const auto& Pawn) { return Pawn.Get() == Selected; });
	bool bChanged = NewTargets != TargetPawns || TargetSelector->GetOptionCount() != Options.Num();
	for (int32 Option = 0; !bChanged && Option < Options.Num(); ++Option) { bChanged = TargetSelector->GetOptionAtIndex(Option) != Options[Option]; }
	TGuardValue<bool> Sync(bSyncingTargets, true);
	if (bChanged)
	{
		TargetPawns = MoveTemp(NewTargets);
		TargetSelector->ClearOptions();
		for (const FString& Option : Options) { TargetSelector->AddOption(Option); }
	}
	const int32 SelectedIndex = Index == INDEX_NONE ? 0 : Index;
	if (TargetSelector->GetSelectedIndex() != SelectedIndex) { TargetSelector->SetSelectedIndex(SelectedIndex); }
}

void UCharacterObservationPanel::HandleTargetSelectionChanged(FString SelectedItem, ESelectInfo::Type)
{
	if (bSyncingTargets || !ObservationSubsystem.IsValid()) { return; }
	const int32 Index = TargetSelector->FindOptionIndex(SelectedItem);
	if (!TargetPawns.IsValidIndex(Index) || (Index > 0 && !TargetPawns[Index].IsValid())) { RefreshTargets(); return; }
	if (ObservationSubsystem->SetObservedPawn(TargetPawns[Index].Get()))
	{
		bFrozen = false;
		if (ObservationSubsystem->GetPawnObservationSnapshot().IsValid())
		{
			// Changing follow mode on the same Pawn can reuse its sample, including during world pause.
			HandleSnapshotUpdated(ObservationSubsystem->GetPawnObservationSnapshot());
			RefreshDisplay();
		}
		else { ClearSnapshot(); }
		PageScrolls[0]->SetScrollOffset(0.0f);
		PageScrolls[2]->SetScrollOffset(0.0f);
	}
}

void UCharacterObservationPanel::SetPoseValues(FName Prefix, const FObservedPoseSample& Pose)
{
	const bool bAvailable = Pose.IsAvailable();
	const FVector& Position = Pose.Value.ZeroOriginPosition;
	SetValue(Key(Prefix, TEXT("_Position")), bAvailable ? FString::Printf(TEXT("%.2f / %.2f / %.2f"), Position.X, Position.Y, Position.Z) : Unavailable);
	SetValue(Key(Prefix, TEXT("_PositionX")), bAvailable ? Number(Position.X) : Unavailable);
	SetValue(Key(Prefix, TEXT("_PositionY")), bAvailable ? Number(Position.Y) : Unavailable);
	SetValue(Key(Prefix, TEXT("_PositionZ")), bAvailable ? Number(Position.Z) : Unavailable);
	const FRotator& Rotation = Pose.Value.Rotation;
	SetValue(Key(Prefix, TEXT("_Facing")), bAvailable ? FString::Printf(TEXT("%.2f / %.2f / %.2f"), Rotation.Pitch, Rotation.Yaw, Rotation.Roll) : Unavailable);
}

void UCharacterObservationPanel::SetMovementValues(FName Prefix, const FObservedMotion& State)
{
	SetPoseValues(Prefix, State.Pose);
	const bool bVelocity = State.VelocityStatus == EObservationAvailability::Available;
	SetValue(Key(Prefix, TEXT("_Velocity")), bVelocity ? FString::Printf(TEXT("%.2f / %.2f / %.2f"), State.Velocity.X, State.Velocity.Y, State.Velocity.Z) : Unavailable);
	SetValue(Key(Prefix, TEXT("_VelocityX")), bVelocity ? Number(State.Velocity.X) : Unavailable);
	SetValue(Key(Prefix, TEXT("_VelocityY")), bVelocity ? Number(State.Velocity.Y) : Unavailable);
	SetValue(Key(Prefix, TEXT("_VelocityZ")), bVelocity ? Number(State.Velocity.Z) : Unavailable);
	SetValue(Key(Prefix, TEXT("_Movement")), MovementName(State));
	SetValue(Key(Prefix, TEXT("_Action")), State.CrouchStatus == EObservationAvailability::Available ? (State.bCrouched ? TEXT("Crouched") : TEXT("Standing")) : Availability(State.CrouchStatus));
}

void UCharacterObservationPanel::RenderSnapshot()
{
	const FPawnObservationFrame* Frame = DisplaySnapshot.Get();
	const FPawnObservationCommon EmptyCommon;
	const FPawnObservationCommon& Common = Frame ? Frame->GetCommon() : EmptyCommon;
	const FAutonomousPawnObservation* Auto = Frame ? Frame->GetAutonomous() : nullptr;
	const FSimulatedPawnObservation* Sim = Frame ? Frame->GetSimulated() : nullptr;
	const FObservedMovePair* Pair = Auto && Auto->LastValidPair.IsAvailable() ? &Auto->LastValidPair : nullptr;
	Cards[0]->SetVisibility(Auto || Sim ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	Cards[1]->SetVisibility(Auto ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	Cards[2]->SetVisibility(ESlateVisibility::Visible);
	Cards[3]->SetVisibility(Frame && Common.VisualCapsulePose.Status != EObservationAvailability::NotApplicable ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	PredictionStats->SetVisibility(Auto ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	TabButtons[2]->SetVisibility(Auto ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	SetValue(TEXT("GhostLegend"), Auto ? TEXT("Ghosts: server red / prediction blue \u00b7 same acknowledged Move ID.")
		: (Sim ? TEXT("Ghosts: decoded reference red / unsmoothed capsule cyan. Mesh shows smoothing.")
		: TEXT("Current local authority or Pawn state.")));
	SetValue(TEXT("Current_Title"), Sim ? TEXT("Current capsule \u00b7 Unsmoothed") : TEXT("Current local state"));
	Values.FindChecked(TEXT("Current_Title"))->SetColorAndOpacity(FSlateColor(Sim ? SimulationColor : FLinearColor(0.85f, 0.91f, 0.96f)));
	SetValue(TEXT("Current_Time_Label"), TEXT("Local game sample (s)"));
	SetValue(TEXT("Current_Time"), Frame ? Number(Common.SampleTime.GameTimeSeconds) : Unavailable);
	SetValue(TEXT("Current_Availability"), Availability(Common.Current.Pose.Status));
	SetMovementValues(TEXT("Current"), Common.Current);
	SetValue(TEXT("Server_Title"), Sim ? TEXT("Decoded network reference") : TEXT("Server \u00b7 Paired move"));
	SetValue(TEXT("Prediction_Title"), TEXT("Prediction \u00b7 Before response"));
	SetMovementValues(TEXT("Server"), Pair ? Pair->ServerState : FObservedMotion{});
	SetMovementValues(TEXT("Prediction"), Pair ? Pair->PredictionBeforeResponse.EndState : FObservedMotion{});
	SetValue(TEXT("Server_Time_Label"), Sim ? TEXT("Received (local game s)") : TEXT("Native move timestamp (s)"));
	SetValue(TEXT("Prediction_Time_Label"), TEXT("Same native timestamp (s)"));
	SetValue(TEXT("Server_Time"), Pair ? Number(Pair->PredictionBeforeResponse.NativeTimeStamp) : Unavailable);
	SetValue(TEXT("Prediction_Time"), Pair ? Number(Pair->PredictionBeforeResponse.NativeTimeStamp) : Unavailable);
	const FString PairStatus = Auto ? Availability(Auto->LastValidPair.Status) : Unavailable;
	SetValue(TEXT("Server_Availability"), Pair ? TEXT("Captured server state for the paired Move ID") : PairStatus + TEXT(" \u00b7 same-move pair"));
	SetValue(TEXT("Prediction_Availability"), Pair ? TEXT("Latest saved end state, copied before this response") : PairStatus + TEXT(" \u00b7 same-move pair"));
	if (Sim)
	{
		SetPoseValues(TEXT("Server"), Sim->LastDecodedReference);
		SetValue(TEXT("Server_Availability"), Availability(Sim->LastDecodedReference.Status) + TEXT(" \u00b7 pose only, native base resolved"));
		SetValue(TEXT("Server_Time"), Sim->LastDecodedReference.IsAvailable() ? Number(Sim->ReferenceReceivedAt.GameTimeSeconds) : Unavailable);
	}
	for (const TCHAR* Suffix : {TEXT("_Velocity"), TEXT("_Movement"), TEXT("_Action")})
	{
		Values.FindChecked(Key(TEXT("Server"), Suffix))->GetParent()->SetVisibility(Sim ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		Values.FindChecked(Key(TEXT("Render"), Suffix))->GetParent()->SetVisibility(ESlateVisibility::Collapsed);
	}
	SetPoseValues(TEXT("Render"), Common.VisualCapsulePose);
	SetValue(TEXT("Render_Title"), TEXT("Visual capsule pose"));
	SetValue(TEXT("Render_Availability"), Availability(Common.VisualCapsulePose.Status) + TEXT(" \u00b7 unsmoothed Mesh baseline removed"));
	SetValue(TEXT("Render_Time_Label"), TEXT("Local game sample (s)"));
	SetValue(TEXT("Render_Time"), Frame ? Number(Common.SampleTime.GameTimeSeconds) : Unavailable);
	Values.FindChecked(TEXT("ReferenceAge"))->GetParent()->SetVisibility(Sim ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	SetValue(TEXT("ReferenceAge"), Sim && Sim->LastDecodedReference.IsAvailable() ? Number(ReferenceAgeMs(DisplaySnapshot)) : Unavailable);
	SetValue(TEXT("SmoothingMode"), Common.Smoothing.Status == EObservationAvailability::Available ? StaticEnum<ENetworkSmoothingMode>()->GetDisplayNameTextByValue(static_cast<int64>(Common.Smoothing.Mode)).ToString() : Availability(Common.Smoothing.Status));
	SetValue(TEXT("Smoothing"), Common.Smoothing.Status == EObservationAvailability::Available ? (Common.Smoothing.bComplete ? TEXT("Complete") : TEXT("Active")) : Unavailable);
	SetValue(TEXT("VisualOffset"), Common.VisualCapsulePose.IsAvailable() && Common.Current.Pose.IsAvailable()
		? Number(FVector::Distance(Common.VisualCapsulePose.Value.ZeroOriginPosition, Common.Current.Pose.Value.ZeroOriginPosition)) : Unavailable);
	SetValue(TEXT("MovementBase"), Common.Current.MovementBase.Status == EObservationAvailability::Available
		? (Common.Current.MovementBase.ObjectPath.IsEmpty() ? TEXT("None") : Common.Current.MovementBase.ObjectPath + TEXT(" / ") + Common.Current.MovementBase.BoneName.ToString())
		: Availability(Common.Current.MovementBase.Status));
	SetValue(TEXT("ConsumedInput"), Common.Input.ConsumedMovementStatus == EObservationAvailability::Available ? VectorText(Common.Input.LastConsumedWorldMovementDirection) : Unavailable);
	SetValue(TEXT("JumpRequest"), Common.Input.JumpRequestStatus == EObservationAvailability::Available ? (Common.Input.bJumpRequestedAtSample ? TEXT("Requested") : TEXT("Released")) : Availability(Common.Input.JumpRequestStatus));
	const bool bPrediction = Auto && Auto->PredictionStatus == EObservationAvailability::Available;
	SetValue(TEXT("GoodACKs"), Auto && Auto->NativeResponsesStatus == EObservationAvailability::Available ? FString::Printf(TEXT("%lld"), Auto->AcceptedGoodAckCount) : Unavailable);
	SetValue(TEXT("Corrections"), Auto && Auto->NativeResponsesStatus == EObservationAvailability::Available ? FString::Printf(TEXT("%lld"), Auto->AcceptedCorrectionCount) : Unavailable);
	SetValue(TEXT("LastResponse"), Auto && Auto->LatestAcceptedResponse.Status == EObservationAvailability::Available
		? FString(Auto->LatestAcceptedResponse.Kind == EObservedMoveResponse::Correction ? TEXT("Correction") : TEXT("Good ACK")) + TEXT(" \u00b7 ") + PairingName(Auto->LatestAcceptedResponse.Pairing)
		: (Auto ? PairingName(Auto->LatestAcceptedResponse.Pairing) : Unavailable));
	SetValue(TEXT("ServerSequence"), Pair ? FString::Printf(TEXT("%lld"), Pair->ServerSampleSequence) : Unavailable);
	SetValue(TEXT("ServerTime"), Pair ? Number(Pair->ServerGameTimeSeconds) : Unavailable);
	SetValue(TEXT("Unacked"), bPrediction ? FString::FromInt(Auto->UnacknowledgedMoves.Num()) : Unavailable);
	SetValue(TEXT("UnackedDuration"), bPrediction ? Number(Auto->GetUnacknowledgedDurationSeconds() * 1000.0) : Unavailable);
	SetValue(TEXT("PairedMoveId"), Pair ? Pair->PredictionBeforeResponse.MoveId.ToString() : Unavailable);
	SetValue(TEXT("PairedResponse"), Pair ? FString::Printf(TEXT("%s \u00b7 %.3f"),
		Pair->Response.Kind == EObservedMoveResponse::Correction ? TEXT("Correction") : TEXT("Good ACK"), Pair->Response.ReceivedAt.GameTimeSeconds) : Unavailable);
	const FObservedMotionDifference Difference = Pair ? PawnObservation::Difference(*Pair) : FObservedMotionDifference{};
	SetValue(TEXT("PairDifference"), Difference.Status == EObservationAvailability::Available
		? FString::Printf(TEXT("%.2f cm \u00b7 %.2f deg \u00b7 %.2f cm/s"), Difference.PositionDistanceCm, Difference.RotationAngleDegrees, Difference.VelocityDistanceCmPerSecond) : Unavailable);
	const FObservedMovePair* Correction = Auto && Auto->LastValidCorrection.IsAvailable() ? &Auto->LastValidCorrection : nullptr;
	const FObservedMotionDifference CorrectionDifference = Correction ? PawnObservation::Difference(*Correction) : FObservedMotionDifference{};
	SetValue(TEXT("LastCorrection"), Correction ? FString::Printf(TEXT("%s\nreceived %.3f local s \u00b7 server %.3f s\n%.2f cm / %.2f deg / %.2f cm/s\nServer: %s\nBefore response: %s"),
		*Correction->PredictionBeforeResponse.MoveId.ToString(), Correction->Response.ReceivedAt.GameTimeSeconds, Correction->ServerGameTimeSeconds,
		CorrectionDifference.PositionDistanceCm, CorrectionDifference.RotationAngleDegrees, CorrectionDifference.VelocityDistanceCmPerSecond,
		*VectorText(Correction->ServerState.Pose.Value.ZeroOriginPosition), *VectorText(Correction->PredictionBeforeResponse.EndState.Pose.Value.ZeroOriginPosition)) : TEXT("No valid correction pair received"));
}

void UCharacterObservationPanel::RenderMoves()
{
	const FAutonomousPawnObservation* Auto = DisplaySnapshot.Get() ? DisplaySnapshot.Get()->GetAutonomous() : nullptr;
	const int32 Count = Auto ? Auto->UnacknowledgedMoves.Num() : 0;
	SetValue(TEXT("MovesSummary"), Auto && Auto->PredictionStatus == EObservationAvailability::Available
		? FString::Printf(TEXT("%d unacked moves \u00b7 %.1f ms total"), Count, UnackedMilliseconds(DisplaySnapshot))
		: (Auto ? Availability(Auto->PredictionStatus) : TEXT("Not applicable")));
	while (MoveRows.Num() < Count)
	{
		UBorder* Row = WidgetTree->ConstructWidget<UBorder>();
		Row->SetPadding(FMargin(12.0f));
		UTextBlock* Text = MakeText(NAME_None, TEXT(""), 13);
		Row->SetContent(Text);
		MovesList->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 6.0f));
		MoveRows.Add(Row);
		MoveTexts.Add(Text);
	}
	for (int32 Index = 0; Index < MoveRows.Num(); ++Index)
	{
		const bool bVisible = Index < Count;
		MoveRows[Index]->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (!bVisible) { continue; }
		const FObservedSavedMove& Move = Auto->UnacknowledgedMoves[Index];
		MoveRows[Index]->SetBrushColor(Index == Count - 1 ? FLinearColor(0.04f, 0.13f, 0.23f) : CardSurface);
		MoveTexts[Index]->SetText(FText::FromString(FString::Printf(
			TEXT("%s\nnative timestamp %.3f s \u00b7 duration %.2f ms%s\nPosition (zero origin cm) %s\nVelocity (cm/s) %s\n%s \u00b7 actual crouch %s\nCommand summary: accel %s \u00b7 jump %s / crouch %s"),
			*Move.MoveId.ToString(), Move.NativeTimeStamp, Move.DeltaTimeSeconds * 1000.0f,
			Move.bBeforeTimeStampReset ? TEXT(" \u00b7 BEFORE RESET") : TEXT(""), *VectorText(Move.EndState.Pose.Value.ZeroOriginPosition), *VectorText(Move.EndState.Velocity),
			*MovementName(Move.EndState), Move.EndState.CrouchStatus == EObservationAvailability::Available ? (Move.EndState.bCrouched ? TEXT("yes") : TEXT("no")) : TEXT("unknown"),
			*VectorText(Move.Command.Acceleration), Move.Command.bJumpRequested ? TEXT("yes") : TEXT("no"), Move.Command.bCrouchRequested ? TEXT("yes") : TEXT("no"))));
	}
}

void UCharacterObservationPanel::HandleAdvanced()
{
	const bool bShow = AdvancedEditors->GetVisibility() == ESlateVisibility::Collapsed;
	AdvancedEditors->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	SetValue(TEXT("AdvancedButton_Label"), bShow ? TEXT("Hide advanced base settings") : TEXT("Show advanced base settings"));
}

void UCharacterObservationPanel::UpdateScenarioControls()
{
	// Type contract (caller guaranteed): CreateScenarioControls keeps the four ScenarioNames in enum
	// order (BurstLoss through BandwidthLimit), with a valid selection before this callback runs.
	const ENetworkFaultScenario Scenario = static_cast<ENetworkFaultScenario>(ScenarioSelector->GetSelectedIndex() + 1);
	const bool bBandwidth = Scenario == ENetworkFaultScenario::BandwidthLimit;
	ScenarioRepeat->GetParent()->SetVisibility(bBandwidth ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	for (const auto& Pair : ScenarioInputs)
	{
		const bool bShow = (Pair.Key == TEXT("BurstPacketCount") && Scenario == ENetworkFaultScenario::BurstLoss)
			|| (Pair.Key == TEXT("Duration") && (Scenario == ENetworkFaultScenario::Blackout || Scenario == ENetworkFaultScenario::LatencySpike))
			|| (Pair.Key == TEXT("AdditionalDelay") && Scenario == ENetworkFaultScenario::LatencySpike)
			|| (Pair.Key == TEXT("Bandwidth") && bBandwidth)
			|| (Pair.Key == TEXT("RepeatInterval") && !bBandwidth && ScenarioRepeat->IsChecked());
		Pair.Value->GetParent()->GetParent()->GetParent()->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UCharacterObservationPanel::UpdateDraftLabels()
{
	SetValue(TEXT("BaseDraft"), bFaultInputsDirty ? TEXT("UNAPPLIED DRAFT \u00b7 Apply base or revert") : TEXT("Draft matches applied settings"));
	SetValue(TEXT("ScenarioDraft"), bScenarioInputsDirty ? TEXT("UNAPPLIED DRAFT \u00b7 Run scenario or revert") : TEXT("Draft matches configured scenario"));
}

void UCharacterObservationPanel::HandleFaultStateChanged()
{
	if (ConnectionSubsystem.IsValid()) { ConnectionSubsystem->RefreshConfiguration(); }
	const FConnectionObservation& ConnectionData = ReadConnection(ConnectionSubsystem.Get());
	const FNetworkFaultSimulationSettings& Settings = ConnectionData.PacketSettings;
	const FText& Error = ConnectionData.PacketSettingsError;
	const bool bAvailable = ConnectionData.PacketSettingsStatus == EObservationAvailability::Available;
	if (InputConnection != ConnectionData.Connection)
	{
		InputConnection = ConnectionData.Connection;
		bFaultInputsDirty = false;
		bScenarioInputsDirty = false;
		SetText(FaultMessage, TEXT(""));
		SetText(ScenarioMessage, TEXT(""));
	}
	if (bAvailable && !bFaultInputsDirty) { ReadBackFaultInputs(); }
	ApplyButton->SetIsEnabled(bAvailable);
	RevertButton->SetIsEnabled(bAvailable);
	ResetButton->SetIsEnabled(bAvailable && ConnectionData.bFaultSimulationActive);
	ApplyButton->SetToolTipText(bAvailable ? FText::FromString(TEXT("Applies every base parameter, including advanced settings.")) : Error);
	for (const FFaultField& Field : FaultFields)
	{
		FaultInputs.FindChecked(Field.Name)->SetIsEnabled(bAvailable);
		SetValue(Key(Field.Name, TEXT("_Applied")), bAvailable ? FString::FromInt(Settings.*Field.Member) : Unavailable);
	}
	SetText(FaultStatus, bAvailable
		? FString::Printf(TEXT("%s %.1f ms \u00b7 Send loss %d%% / Receive loss %d%%"),
			LastViewportSize.Y < 620.0 ? TEXT("Base nominal") : TEXT("APPLIED BASE \u00b7 nominal"),
			FNetworkFaultSimulationSettings::GetNominalLatencyMs(Settings.ToNative()), Settings.PktLoss, Settings.PktIncomingLoss)
		: (Error.IsEmpty() ? TEXT("Unavailable \u00b7 no open client connection") : Error.ToString()));
	ReadBackScenarioInputs();
	RefreshScenarioState();
	UpdateDraftLabels();
}

void UCharacterObservationPanel::RefreshScenarioState()
{
	const FConnectionObservation& ConnectionData = ReadConnection(ConnectionSubsystem.Get());
	const FNetworkFaultScenarioState& State = ConnectionData.ScenarioState;
	const FText& Error = ConnectionData.ScenarioError;
	const bool bAvailable = ConnectionData.ScenarioStatus == EObservationAvailability::Available;
	ScenarioStartButton->SetIsEnabled(bAvailable);
	ScenarioRevertButton->SetIsEnabled(bAvailable);
	ScenarioStopButton->SetIsEnabled(bAvailable && (State.bRunning || State.PendingPackets > 0));
	ScenarioSelector->SetIsEnabled(bAvailable);
	ScenarioRepeat->SetIsEnabled(bAvailable);
	ScenarioStartButton->SetToolTipText(bAvailable ? FText::FromString(TEXT("Starts the draft on outgoing packets. Replaces the previous scenario.")) : Error);
	for (const auto& Pair : ScenarioInputs) { Pair.Value->SetIsEnabled(bAvailable); }
	if (bAvailable)
	{
		const int32 Index = static_cast<int32>(State.Scenario) - 1;
		const TCHAR* Name = Index >= 0 && Index < UE_ARRAY_COUNT(ScenarioNames) ? ScenarioNames[Index] : TEXT("None");
		const TCHAR* Phase = State.bRunning ? (State.bFaultWindowActive ? TEXT("Active") : TEXT("Waiting"))
			: (State.PendingPackets > 0 ? TEXT("Draining") : TEXT("Stopped"));
		SetText(ScenarioStatus, FString::Printf(TEXT("%s \u00b7 %s \u00b7 spike +%.1f ms\nScenario queue %d packets / %lld bytes \u00b7 dropped %lld (overflow %lld)"),
			Name, Phase, State.AdditionalDelayMs, State.PendingPackets, State.PendingBytes, State.DroppedPackets, State.OverflowDroppedPackets));
		SetValue(TEXT("HUD_Scenario"), FString::Printf(TEXT("Scenario: %s \u00b7 %s"), Name, Phase));
		const FNetworkFaultScenarioSettings& Settings = ConnectionData.ScenarioSettings;
		if (bAvailable)
		{
			FString Configuration;
			switch (Settings.Scenario)
			{
			case ENetworkFaultScenario::BurstLoss: Configuration = FString::Printf(TEXT("%d packets"), Settings.BurstPacketCount); break;
			case ENetworkFaultScenario::Blackout: Configuration = FString::Printf(TEXT("%.2f s"), Settings.DurationSeconds); break;
			case ENetworkFaultScenario::LatencySpike: Configuration = FString::Printf(TEXT("+%.1f ms for %.2f s"), Settings.AdditionalDelayMs, Settings.DurationSeconds); break;
			case ENetworkFaultScenario::BandwidthLimit: Configuration = FString::Printf(TEXT("%.1f Kbps \u00b7 continuous"), Settings.BandwidthKbps); break;
			default: break;
			}
			if (Settings.bRepeat) { Configuration += FString::Printf(TEXT(" \u00b7 repeats every %.2f s"), Settings.RepeatIntervalSeconds); }
			SetValue(TEXT("ScenarioApplied"), FString(TEXT("CONFIGURED \u00b7 ")) + Configuration);
		}
	}
	else
	{
		SetText(ScenarioStatus, Error.IsEmpty() ? TEXT("Unavailable \u00b7 no open client connection") : Error.ToString());
		SetValue(TEXT("HUD_Scenario"), TEXT("Scenario: unavailable"));
		SetValue(TEXT("ScenarioApplied"), TEXT(""));
	}
}

void UCharacterObservationPanel::ReadBackFaultInputs()
{
	const FConnectionObservation& ConnectionData = ReadConnection(ConnectionSubsystem.Get());
	if (ConnectionData.PacketSettingsStatus != EObservationAvailability::Available) { return; }
	const FNetworkFaultSimulationSettings& Settings = ConnectionData.PacketSettings;
	TGuardValue<bool> Syncing(bSyncingFaultInputs, true);
	for (const FFaultField& Field : FaultFields)
	{
		UEditableTextBox* Input = FaultInputs.FindChecked(Field.Name);
		const FString Value = FString::FromInt(Settings.*Field.Member);
		if (Input->GetText().ToString() != Value) { Input->SetText(FText::FromString(Value)); }
		Input->SetError(FText::GetEmpty());
	}
	bFaultInputsDirty = false;
}

void UCharacterObservationPanel::ReadBackScenarioInputs()
{
	if (bScenarioInputsDirty || !ConnectionSubsystem.IsValid()) { return; }
	const FConnectionObservation& ConnectionData = ReadConnection(ConnectionSubsystem.Get());
	if (ConnectionData.ScenarioStatus != EObservationAvailability::Available) { return; }
	const FNetworkFaultScenarioSettings& Settings = ConnectionData.ScenarioSettings;
	TGuardValue<bool> Syncing(bSyncingScenarioInputs, true);
	const int32 SelectedIndex = static_cast<int32>(Settings.Scenario) - 1;
	if (ScenarioSelector->GetSelectedIndex() != SelectedIndex) { ScenarioSelector->SetSelectedIndex(SelectedIndex); }
	if (ScenarioRepeat->IsChecked() != Settings.bRepeat) { ScenarioRepeat->SetIsChecked(Settings.bRepeat); }
	UEditableTextBox* Burst = ScenarioInputs.FindChecked(TEXT("BurstPacketCount"));
	const FString Count = FString::FromInt(Settings.BurstPacketCount);
	if (Burst->GetText().ToString() != Count) { Burst->SetText(FText::FromString(Count)); }
	for (const FScenarioField& Field : ScenarioFields)
	{
		UEditableTextBox* Input = ScenarioInputs.FindChecked(Field.Name);
		const FString Value = FString::SanitizeFloat(Settings.*Field.Member);
		if (Input->GetText().ToString() != Value) { Input->SetText(FText::FromString(Value)); }
	}
	for (const auto& Pair : ScenarioInputs) { Pair.Value->SetError(FText::GetEmpty()); }
	UpdateScenarioControls();
}

void UCharacterObservationPanel::HandleFaultInputChanged(const FText&)
{
	if (!bSyncingFaultInputs)
	{
		bFaultInputsDirty = true;
		for (const auto& Pair : FaultInputs) { Pair.Value->SetError(FText::GetEmpty()); }
		UpdateDraftLabels();
	}
}

void UCharacterObservationPanel::HandleRevert()
{
	ReadBackFaultInputs();
	SetText(FaultMessage, TEXT("Draft restored to the currently applied settings."));
	HandleFaultStateChanged();
}

void UCharacterObservationPanel::HandleScenarioRevert()
{
	bScenarioInputsDirty = false;
	ReadBackScenarioInputs();
	SetText(ScenarioMessage, TEXT("Draft restored to the configured scenario."));
	HandleFaultStateChanged();
}

void UCharacterObservationPanel::HandleApply()
{
	if (!NetworkFaultSubsystem.IsValid()) { return; }
	FNetworkFaultSimulationSettings Settings;
	for (const FFaultField& Field : FaultFields)
	{
		UEditableTextBox* Input = FaultInputs.FindChecked(Field.Name);
		const FString Text = Input->GetText().ToString().TrimStartAndEnd();
		// Check the entire decimal string and int32 boundary before conversion.
		bool bDigits = !Text.IsEmpty() && Text.Len() <= 10;
		for (TCHAR Character : Text) { bDigits &= Character >= TEXT('0') && Character <= TEXT('9'); }
		const int64 Value = bDigits ? FCString::Atoi64(*Text) : -1;
		if (Value < 0 || Value > MAX_int32)
		{
			const FText Error = FText::FromString(FString::Printf(TEXT("%s: enter an integer from 0 to %d."), Field.Label, MAX_int32));
			Input->SetError(Error);
			FaultMessage->SetText(Error);
			if (!IsCommon(Field.Name) && AdvancedEditors->GetVisibility() == ESlateVisibility::Collapsed) { HandleAdvanced(); }
			PageScrolls[1]->ScrollWidgetIntoView(Input, false);
			return;
		}
		Settings.*Field.Member = static_cast<int32>(Value);
	}
	FText Error;
	if (NetworkFaultSubsystem->ApplySettings(Settings, Error))
	{
		ReadBackFaultInputs();
		SetText(FaultMessage, TEXT("Base settings applied to the current client connection."));
	}
	else { FaultMessage->SetText(Error); }
	HandleFaultStateChanged();
}

void UCharacterObservationPanel::HandleReset()
{
	if (!NetworkFaultSubsystem.IsValid()) { return; }
	FText Error;
	if (NetworkFaultSubsystem->ResetSettings(Error))
	{
		ReadBackFaultInputs();
		bScenarioInputsDirty = false;
		ReadBackScenarioInputs();
		SetText(FaultMessage, TEXT("Original base settings restored. Scenario stopped."));
		SetText(ScenarioMessage, TEXT(""));
	}
	else { FaultMessage->SetText(Error); }
	HandleFaultStateChanged();
}

void UCharacterObservationPanel::HandleScenarioSelectionChanged(FString, ESelectInfo::Type)
{
	if (!bSyncingScenarioInputs) { bScenarioInputsDirty = true; }
	UpdateScenarioControls();
	UpdateDraftLabels();
}

void UCharacterObservationPanel::HandleScenarioRepeatChanged(bool)
{
	if (!bSyncingScenarioInputs) { bScenarioInputsDirty = true; }
	UpdateScenarioControls();
	UpdateDraftLabels();
}

void UCharacterObservationPanel::HandleScenarioInputChanged(const FText&)
{
	if (!bSyncingScenarioInputs)
	{
		bScenarioInputsDirty = true;
		for (const auto& Pair : ScenarioInputs) { Pair.Value->SetError(FText::GetEmpty()); }
		UpdateDraftLabels();
	}
}

void UCharacterObservationPanel::HandleScenarioStart()
{
	if (!NetworkFaultSubsystem.IsValid()) { return; }
	FNetworkFaultScenarioSettings Settings;
	// Type contract (caller guaranteed): the populated selector has a valid index into ScenarioNames,
	// whose four options match ENetworkFaultScenario values 1..4 in CreateScenarioControls.
	Settings.Scenario = static_cast<ENetworkFaultScenario>(ScenarioSelector->GetSelectedIndex() + 1);
	Settings.bRepeat = Settings.Scenario != ENetworkFaultScenario::BandwidthLimit && ScenarioRepeat->IsChecked();
	auto ShowError = [&](UEditableTextBox* Input, const FText& Error)
	{
		Input->SetError(Error);
		ScenarioMessage->SetText(Error);
		PageScrolls[1]->ScrollWidgetIntoView(Input, false);
	};
	if (Settings.Scenario == ENetworkFaultScenario::BurstLoss)
	{
		UEditableTextBox* Input = ScenarioInputs.FindChecked(TEXT("BurstPacketCount"));
		const FString Text = Input->GetText().ToString().TrimStartAndEnd();
		bool bDigits = !Text.IsEmpty() && Text.Len() <= 10;
		for (TCHAR Character : Text) { bDigits &= Character >= TEXT('0') && Character <= TEXT('9'); }
		const int64 Value = bDigits ? FCString::Atoi64(*Text) : -1;
		if (Value <= 0 || Value > MAX_int32)
		{
			ShowError(Input, FText::FromString(TEXT("Packet count must be a positive integer.")));
			return;
		}
		Settings.BurstPacketCount = static_cast<int32>(Value);
	}
	for (const FScenarioField& Field : ScenarioFields)
	{
		UEditableTextBox* Input = ScenarioInputs.FindChecked(Field.Name);
		if (Input->GetParent()->GetParent()->GetParent()->GetVisibility() == ESlateVisibility::Collapsed) { continue; }
		const FString Text = Input->GetText().ToString().TrimStartAndEnd();
		const double Value = FCString::Atod(*Text);
		if (!Text.IsNumeric() || !FMath::IsFinite(Value) || Value <= 0.0 || Value > MAX_flt)
		{
			ShowError(Input, FText::FromString(FString::Printf(TEXT("%s: enter a finite positive number."), Field.Label)));
			return;
		}
		Settings.*Field.Member = static_cast<float>(Value);
	}
	FText Error;
	if (!Settings.Validate(Error))
	{
		ShowError(ScenarioInputs.FindChecked(TEXT("RepeatInterval")), Error);
		return;
	}
	if (NetworkFaultSubsystem->StartScenario(Settings, Error))
	{
		bScenarioInputsDirty = false;
		ReadBackScenarioInputs();
		SetText(ScenarioMessage, TEXT("Scenario started on this client's outgoing packets."));
	}
	else { ScenarioMessage->SetText(Error); }
	HandleFaultStateChanged();
}

void UCharacterObservationPanel::HandleScenarioStop()
{
	if (!NetworkFaultSubsystem.IsValid()) { return; }
	FText Error;
	if (NetworkFaultSubsystem->StopScenario(Error)) { SetText(ScenarioMessage, TEXT("Scenario stopped; queued packets sent in order. Base unchanged.")); }
	else { ScenarioMessage->SetText(Error); }
	HandleFaultStateChanged();
}
