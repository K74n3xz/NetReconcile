#include "ObservationPlayerController.h"
#include "UI/CharacterObservationPanel.h"

#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Framework/Application/IInputProcessor.h"
#include "InputCoreTypes.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

/** Routes inspector shortcuts even when a text field or a separate combo popup has focus. */
class FObservationInputProcessor : public IInputProcessor
{
public:
	explicit inline FObservationInputProcessor(AObservationPlayerController* InOwner) : Owner(InOwner) {}
	virtual inline void Tick(float, FSlateApplication&, TSharedRef<ICursor>) override {}

	virtual inline bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
	{
		if (!OwnsFocus(Event)) { return false; }
		if (Event.GetKey() == EKeys::F2)
		{
			bHandledF2 = true;
			if (!Event.IsRepeat()) { Owner->ToggleObservationPanelExpansion(); }
			return true;
		}
		if (Event.GetKey() == EKeys::LeftAlt && Owner->IsObservationPanelExpanded())
		{
			bHandledAlt = true;
			if (!Event.IsRepeat()) { Owner->SetObservationUIInput(!Owner->IsObservationUIInputEnabled()); }
			return true;
		}
		return false;
	}

	virtual inline bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override
	{
		bool* Handled = Event.GetKey() == EKeys::F2 ? &bHandledF2 : (Event.GetKey() == EKeys::LeftAlt ? &bHandledAlt : nullptr);
		if (!Handled) { return false; }
		const bool bConsume = *Handled && OwnsFocus(Event);
		*Handled = false;
		return bConsume;
	}

private:
	TWeakObjectPtr<AObservationPlayerController> Owner;
	bool bHandledF2 = false;
	bool bHandledAlt = false;

	inline bool OwnsFocus(const FKeyEvent& Event) const
	{
		return Owner.IsValid() && Owner->HasObservationInputFocus()
			&& Event.GetUserIndex() == Owner->GetLocalPlayer()->GetSlateUser()->GetUserIndex();
	}
};

void AObservationPlayerController::ReceivedPlayer()
{
	Super::ReceivedPlayer();
	if (HasActorBegunPlay()) { EnsureObservationPanel(); }
}

void AObservationPlayerController::PostSeamlessTravel()
{
	Super::PostSeamlessTravel();
	SetObservationPanelExpanded(false);
	if (ObservationPanel) { ObservationPanel->RemoveFromParent(); ObservationPanel = nullptr; }
	EnsureObservationPanel();
}

void AObservationPlayerController::ToggleObservationPanelExpansion()
{
	EnsureObservationPanel();
	if (ObservationPanel && IsLocalPlayerController()) { SetObservationPanelExpanded(!bObservationPanelExpanded); }
}

void AObservationPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	EnsureObservationPanel();
	UpdateObservationUIInput();
}

bool AObservationPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	// Slate still receives editing input; only this player's unhandled game input is consumed.
	return bObservationUIInput || Super::InputKey(Params);
}

void AObservationPlayerController::BeginPlay()
{
	Super::BeginPlay();
	EnsureObservationPanel();
}

void AObservationPlayerController::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	SetObservationPanelExpanded(false);
	if (ObservationInputProcessor && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(ObservationInputProcessor);
	}
	ObservationInputProcessor.Reset();
	if (ObservationPanel) { ObservationPanel->RemoveFromParent(); ObservationPanel = nullptr; }
	Super::EndPlay(EndPlayReason);
}

void AObservationPlayerController::EnsureObservationPanel()
{
	if (!IsLocalPlayerController() || !GetLocalPlayer()) { return; }
	if (!ObservationPanel)
	{
		ObservationPanel = CreateWidget<UCharacterObservationPanel>(this, UCharacterObservationPanel::StaticClass());
	}
	// PIE can receive the local controller before its player viewport exists. Retry this same instance.
	if (ObservationPanel && !ObservationPanel->IsInViewport() && GetWorld()->GetGameViewport()
		&& ObservationPanel->AddToPlayerScreen(10))
	{
		ObservationPanel->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		ObservationPanel->SetExpanded(bObservationPanelExpanded);
		ObservationPanel->SetUIInputEnabled(bObservationUIInput);
	}
	if (!ObservationInputProcessor && ObservationPanel && ObservationPanel->IsInViewport() && FSlateApplication::IsInitialized())
	{
		ObservationInputProcessor = MakeShared<FObservationInputProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(ObservationInputProcessor);
	}
}

void AObservationPlayerController::SetObservationPanelExpanded(bool bExpanded)
{
	if (bObservationPanelExpanded == bExpanded) { return; }
	bObservationPanelExpanded = bExpanded;
	if (!bExpanded) { SetObservationUIInput(false); }
	if (ObservationPanel) { ObservationPanel->SetExpanded(bExpanded); }
}

bool AObservationPlayerController::HasObservationInputFocus() const
{
	if (!IsLocalPlayerController() || !GetLocalPlayer() || !FSlateApplication::IsInitialized()) { return false; }
	const UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	const TSharedPtr<SWindow> Window = Viewport ? Viewport->GetWindow() : nullptr;
	const TSharedPtr<const FSlateUser> User = GetLocalPlayer()->GetSlateUser();
	const FSlateApplication& Slate = FSlateApplication::Get();
	if (!Window || !User || !Slate.IsActive() || Slate.GetActiveTopLevelRegularWindow() != Window) { return false; }
	const auto View = Viewport->GetGameViewportWidget();
	const auto Panel = ObservationPanel ? ObservationPanel->GetCachedWidget() : nullptr;
	const int32 UserIndex = User->GetUserIndex();
	if ((View && (View->HasUserFocus(UserIndex).IsSet() || View->HasUserFocusedDescendants(UserIndex)))
		|| (Panel && (Panel->HasUserFocus(UserIndex).IsSet() || Panel->HasUserFocusedDescendants(UserIndex)))) { return true; }
	// A combo-box popup has its own window and focus path, but belongs to this inspector's window.
	const auto Focused = Slate.GetUserFocusedWidget(UserIndex);
	const auto FocusedWindow = Focused ? Slate.FindWidgetWindow(Focused.ToSharedRef()) : nullptr;
	return bObservationUIInput && FocusedWindow && FocusedWindow != Window && FocusedWindow->IsDescendantOf(Window);
}

void AObservationPlayerController::UpdateObservationUIInput()
{
	if (bObservationUIInput && (!bObservationPanelExpanded || !ObservationPanel || !ObservationPanel->IsInViewport()
		|| !HasObservationInputFocus())) { SetObservationUIInput(false); }
}

void AObservationPlayerController::SetObservationUIInput(bool bEnabled)
{
	if (bObservationUIInput == bEnabled) { return; }
	const bool bHadFocus = HasObservationInputFocus();
	if (!bEnabled && bHadFocus)
	{
		// Dismiss only this player's focused menu before returning focus to the game viewport.
		const auto Focused = FSlateApplication::Get().GetUserFocusedWidget(GetLocalPlayer()->GetSlateUser()->GetUserIndex());
		if (Focused) { FSlateApplication::Get().DismissMenuByWidget(Focused.ToSharedRef()); }
	}
	bObservationUIInput = bEnabled;
	FlushPressedKeys();
	SetIgnoreMoveInput(bEnabled);
	SetIgnoreLookInput(bEnabled);
	bShowMouseCursor = bEnabled;
	if (ObservationPanel) { ObservationPanel->SetUIInputEnabled(bEnabled); }
	if (bEnabled && ObservationPanel)
	{
		FInputModeGameAndUI Mode;
		Mode.SetWidgetToFocus(ObservationPanel->TakeWidget());
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
	}
	else
	{
		UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
		if (Viewport)
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				const auto Other = Cast<AObservationPlayerController>(It->Get());
				if (Other && Other != this && Other->bObservationUIInput && Other->GetLocalPlayer()
					&& Other->GetLocalPlayer()->ViewportClient == Viewport)
				{
					// Capture belongs to the shared viewport. Leave the other inspector's mode intact.
					if (bHadFocus && Viewport->GetGameViewportWidget())
					{
						GetLocalPlayer()->GetSlateOperations().SetUserFocus(Viewport->GetGameViewportWidget().ToSharedRef());
					}
					return;
				}
			}
		}
		FInputModeGameOnly Mode;
		Mode.SetConsumeCaptureMouseDown(false);
		if (bHadFocus) { SetInputMode(Mode); }
		else if (GetWorld() && GetWorld()->GetGameViewport() && GetLocalPlayer())
		{
			// Reset viewport input/capture settings without applying focus or capture to an inactive window.
			SetInputMode(Mode);
			GetLocalPlayer()->GetSlateOperations() = FReply::Unhandled();
		}
	}
}
