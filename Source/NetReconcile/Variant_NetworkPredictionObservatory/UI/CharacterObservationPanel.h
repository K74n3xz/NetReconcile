#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "../Character/Model/PawnObservationSnapshot.h"
#include "CharacterObservationPanel.generated.h"

class UBorder;
class UButton;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class UGridPanel;
class ULocalPlayerObservationSubsystem;
class UNetworkFaultSimulationSubsystem;
class UConnectionObservationSubsystem;
class UNetConnection;
class APawn;
class UScrollBox;
class UScaleBox;
class USizeBox;
class UTextBlock;
class UUniformGridPanel;
class UVerticalBox;
class UWidgetSwitcher;

/** Native HUD and F2 inspector for the owning local player's observation data. */
UCLASS()
class NETRECONCILE_API UCharacterObservationPanel : public UUserWidget
{
	GENERATED_BODY()

	friend class FObservationInspectorCommand;
	friend class FCharacterObservationPanelTest;

public:
	/** Expands or collapses the detailed sidebar. */
	void SetExpanded(bool bInExpanded);
	void SetUIInputEnabled(bool bEnabled);
	void RefreshBindings();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float DeltaTime) override;

private:
	TWeakObjectPtr<ULocalPlayerObservationSubsystem> ObservationSubsystem;
	TWeakObjectPtr<UNetworkFaultSimulationSubsystem> NetworkFaultSubsystem;
	TWeakObjectPtr<UConnectionObservationSubsystem> ConnectionSubsystem;
	TWeakObjectPtr<UNetConnection> InputConnection;
	FPawnObservationSnapshot LiveSnapshot;
	FPawnObservationSnapshot DisplaySnapshot;
	bool bDisplayingSnapshot = false;
	bool bFrozen = false;
	bool bExpanded = false;
	bool bUIInputEnabled = false;
	bool bSnapshotPending = false;
	bool bFaultInputsDirty = false;
	// Suppress draft changes while applied settings are copied into the controls.
	bool bSyncingFaultInputs = false;
	bool bScenarioInputsDirty = false;
	bool bSyncingScenarioInputs = false;
	bool bSyncingTargets = false;
	TArray<TWeakObjectPtr<APawn>> TargetPawns;
	float RefreshElapsed = 0.0f;
	FVector2D LastViewportSize = FVector2D::ZeroVector;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UTextBlock>> Values;
	UPROPERTY(Transient)
	TObjectPtr<UScaleBox> ReadabilityRoot;
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> HudSize;
	UPROPERTY(Transient)
	TObjectPtr<USizeBox> DetailSize;
	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> Pages;
	UPROPERTY(Transient)
	TObjectPtr<UGridPanel> StateCards;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> Cards;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> PredictionStats;
	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> TargetSelector;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> TabButtons;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UScrollBox>> PageScrolls;
	UPROPERTY(Transient)
	TObjectPtr<UButton> FreezeButton;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> MovesList;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> MoveRows;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> MoveTexts;

	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UEditableTextBox>> FaultInputs;
	UPROPERTY(Transient)
	TObjectPtr<UVerticalBox> AdvancedEditors;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FaultStatus;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> FaultMessage;
	UPROPERTY(Transient)
	TObjectPtr<UUniformGridPanel> FaultActions;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ApplyButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ResetButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> RevertButton;
	UPROPERTY(Transient)
	TObjectPtr<UComboBoxString> ScenarioSelector;
	UPROPERTY(Transient)
	TObjectPtr<UCheckBox> ScenarioRepeat;
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UEditableTextBox>> ScenarioInputs;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ScenarioStatus;
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ScenarioMessage;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ScenarioStartButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ScenarioStopButton;
	UPROPERTY(Transient)
	TObjectPtr<UButton> ScenarioRevertButton;

	void BuildInterface();
	UTextBlock* MakeText(FName Name, const FString& Text, int32 Size = 14, bool bBold = false);
	UButton* MakeButton(FName Name, const FString& Label);
	UComboBoxString* MakeComboBox(FName Name);
	UTextBlock* AddValue(UVerticalBox* Section, FName Name, const FString& Label, bool bStacked = false);
	UEditableTextBox* AddInput(UVerticalBox* Section, FName Name, const FString& Label, const FString& InitialValue);
	UBorder* MakeStateCard(FName Prefix, const FString& Title, const FLinearColor& Color);
	void BuildFaultPage(UVerticalBox* Section);
	void CreateScenarioControls(UVerticalBox* Section);
	void UpdateLayout(const FVector2D& Size);
	void SelectPage(int32 Index);
	void RefreshDisplay();
	void RefreshLiveSummary();
	void RefreshTargets();
	void RenderSnapshot();
	void RenderMoves();
	void ClearSnapshot();
	void SetValue(FName Name, const FString& Text);
	void SetMovementValues(FName Prefix, const FObservedMotion& State);
	void SetPoseValues(FName Prefix, const FObservedPoseSample& Pose);
	void UpdateScenarioControls();
	void RefreshScenarioState();
	void ReadBackScenarioInputs();
	void ReadBackFaultInputs();
	void UpdateDraftLabels();

	UFUNCTION()
	void HandleSnapshotUpdated(const FPawnObservationSnapshot& Snapshot);
	UFUNCTION()
	void HandleClose();
	UFUNCTION()
	void HandleStateTab();
	UFUNCTION()
	void HandleFaultTab();
	UFUNCTION()
	void HandleMovesTab();
	UFUNCTION()
	void HandleFreeze();
	UFUNCTION()
	void HandleAdvanced();
	UFUNCTION()
	void HandleFaultStateChanged();
	UFUNCTION()
	void HandleFaultInputChanged(const FText& Text);
	UFUNCTION()
	void HandleApply();
	UFUNCTION()
	void HandleReset();
	UFUNCTION()
	void HandleRevert();
	UFUNCTION()
	void HandleScenarioRevert();
	UFUNCTION()
	UWidget* GenerateComboOption(FString Option);
	UFUNCTION()
	void HandleTargetSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void HandleScenarioSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);
	UFUNCTION()
	void HandleScenarioRepeatChanged(bool bChecked);
	UFUNCTION()
	void HandleScenarioInputChanged(const FText& Text);
	UFUNCTION()
	void HandleScenarioStart();
	UFUNCTION()
	void HandleScenarioStop();
};
