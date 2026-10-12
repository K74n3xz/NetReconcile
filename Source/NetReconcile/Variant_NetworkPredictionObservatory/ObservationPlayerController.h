#pragma once

#include "CoreMinimal.h"
#include "NetReconcilePlayerController.h"
#include "ObservationPlayerController.generated.h"

class IInputProcessor;
class UCharacterObservationPanel;

/** Creates the observation panel only for the owning local player. */
UCLASS(Abstract)
class NETRECONCILE_API AObservationPlayerController : public ANetReconcilePlayerController
{
	GENERATED_BODY()

	friend class FObservationInputProcessor;
	friend class FObservationInspectorCommand;
	friend class UCharacterObservationPanel;
	friend class FCharacterObservationPanelTest;

public:
	virtual void ReceivedPlayer() override;
	virtual void PostSeamlessTravel() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;

	/** Toggles the detailed sidebar while keeping scene controls available. */
	UFUNCTION(BlueprintCallable, Category = "Observation")
	void ToggleObservationPanelExpansion();

	/** True while the detailed sidebar is expanded. */
	UFUNCTION(BlueprintPure, Category = "Observation")
	inline bool IsObservationPanelExpanded() const { return bObservationPanelExpanded; }

	/** True while the expanded inspector is in the editing mode toggled by Left Alt. */
	UFUNCTION(BlueprintPure, Category = "Observation")
	inline bool IsObservationUIInputEnabled() const { return bObservationUIInput; }

	inline UCharacterObservationPanel* GetObservationPanel() const { return ObservationPanel; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

private:
	TSharedPtr<IInputProcessor> ObservationInputProcessor;
	UPROPERTY(Transient)
	TObjectPtr<UCharacterObservationPanel> ObservationPanel;
	bool bObservationPanelExpanded = false;
	bool bObservationUIInput = false;
	void EnsureObservationPanel();
	void SetObservationPanelExpanded(bool bExpanded);
	void UpdateObservationUIInput();
	void SetObservationUIInput(bool bEnabled);
	bool HasObservationInputFocus() const;
};
