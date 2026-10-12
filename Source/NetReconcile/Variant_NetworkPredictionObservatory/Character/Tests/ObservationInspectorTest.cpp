#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Variant_NetworkPredictionObservatory/ObservationPlayerController.h"
#include "Variant_NetworkPredictionObservatory/UI/CharacterObservationPanel.h"
#include "Variant_NetworkPredictionObservatory/Character/LocalPlayerObservationSubsystem.h"
#include "Variant_NetworkPredictionObservatory/Network/ConnectionObservationSubsystem.h"
#include "Variant_NetworkPredictionObservatory/Character/ObservationCharacter.h"
#include "Variant_NetworkPredictionObservatory/Character/ObservationCharacterMovementComponent.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/WidgetSwitcher.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/NetDriver.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "GameFramework/WorldSettings.h"
#include "ImageUtils.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWindow.h"
#include "Widgets/SViewport.h"
#include "Widgets/Text/STextBlock.h"

/** Exercises the actual map entry, two owning-player screens and rendered native widgets. */
class FObservationInspectorCommand : public IAutomationLatentCommand
{
public:
	explicit inline FObservationInspectorCommand(FAutomationTestBase* InTest) : Test(InTest)
	{
		Settings.Reset(DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage()));
		Settings->SetPlayNetMode(PIE_Client);
		Settings->SetRunUnderOneProcess(true);
		Settings->SetPlayNumberOfClients(2);
		Settings->SetServerPort(17889);
		Settings->SetClientWindowSize(FIntPoint(640, 480));
		Settings->NewWindowWidth = 640;
		Settings->NewWindowHeight = 480;
		// These editor settings expose getters only; clear their reflected properties in this transient copy.
		for (FName Property : {FName(TEXT("AdditionalServerGameOptions")), FName(TEXT("ServerMapNameOverride"))})
		{
			// Type contract (caller guaranteed): the named properties remain FString fields on ULevelEditorPlaySettings;
			// Settings is a live transient copy with the exact owning container layout.
			FindFProperty<FStrProperty>(Settings->GetClass(), Property)->SetPropertyValue_InContainer(Settings.Get(), FString());
		}
		FRequestPlaySessionParams Params;
		Params.EditorPlaySettings = Settings.Get();
		Params.GlobalMapOverride = TEXT("/Game/Variant_NetworkPredictionObservatory/Lvl_Observation");
		Params.bAllowOnlineSubsystem = false;
		GEditor->RequestPlaySession(Params);
	}

	virtual inline ~FObservationInspectorCommand() override
	{
		if (SharedInputDevice.IsValid())
		{
			IPlatformInputDeviceMapper::Get().Internal_MapInputDeviceToUser(
				SharedInputDevice, FPlatformUserId::CreateFromInternalId(1), EInputDeviceConnectionState::Disconnected);
		}
	}

	virtual inline bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 360.0)
		{
			Test->AddError(TEXT("Timed out waiting for two PIE clients and the native inspector."));
			GEditor->RequestEndPlayMap();
			return true;
		}
		TArray<AObservationPlayerController*> Clients;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || Context.WorldType != EWorldType::PIE) { continue; }
			if (World->GetNetMode() == NM_DedicatedServer)
			{
				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				{
					if (const AObservationPlayerController* PC = Cast<AObservationPlayerController>(It->Get()))
					{
						if (PC->GetObservationPanel()) { Test->AddError(TEXT("Dedicated PIE server created a panel.")); }
					}
				}
				continue;
			}
			if (World->GetNetMode() != NM_Client) { continue; }
			AObservationPlayerController* Controller = Cast<AObservationPlayerController>(World->GetFirstPlayerController());
			if (!Controller || !Controller->GetPawn() || !Controller->GetObservationPanel()
				|| !Controller->GetObservationPanel()->IsInViewport()) { return false; }
			const ULocalPlayerObservationSubsystem* Observation = Controller->GetLocalPlayer()->GetSubsystem<ULocalPlayerObservationSubsystem>();
			if (!Observation || !Observation->GetPawnObservationSnapshot().IsValid() || Observation->GetWorld()->GetSubsystem<UConnectionObservationSubsystem>()->GetConnectionObservation().Status != EObservationAvailability::Available) { return false; }
			Clients.Add(Controller);
		}
		if (Clients.Num() != 2) { return false; }
		if (Stage == 0)
		{
			for (AObservationPlayerController* Controller : Clients)
			{
				UCharacterObservationPanel* Panel = Controller->GetObservationPanel();
				Test->TestEqual(TEXT("PIE client uses the native panel"), Panel->GetClass(), UCharacterObservationPanel::StaticClass());
				Test->TestEqual(TEXT("Each panel belongs to its own client"), Panel->GetOwningPlayer(), static_cast<APlayerController*>(Controller));
				CheckBounds(*Controller, *Panel->HudSize);
			}
			Target = Clients[0];
			Focus(*Target.Get());
			Input(*Target.Get(), EKeys::W, IE_Pressed, 1.0f);
			Stage = 10;
			NextAction = Now + 0.5;
			return false;
		}
		if (Now < NextAction)
		{
			// Keep mouse motion active across input frames, including an initial Enhanced Input rebuild.
			if (Stage == 1) { Input(*Target.Get(), EKeys::MouseX, IE_Axis, 2.0f); }
			if (Stage == 103 && SharedController.IsValid())
			{
				ViewportInput(*SharedController.Get(), SharedInputDevice, EKeys::Gamepad_LeftY, IE_Axis, 1.0f);
				ViewportInput(*SharedController.Get(), SharedInputDevice, EKeys::Gamepad_RightX, IE_Axis, 0.5f);
			}
			return false;
		}
		if (Stage == 10)
		{
			AObservationPlayerController& Controller = *Target.Get();
			Test->TestTrue(TEXT("Movement key is held before opening details"), Controller.IsInputKeyDown(EKeys::W));
			MovementStart = Controller.GetPawn()->GetActorLocation();
			YawStart = Controller.GetControlRotation().Yaw;
			Press(Controller, EKeys::F2);
			for (AObservationPlayerController* Client : Clients)
			{
				if (Client != &Controller) { Client->ToggleObservationPanelExpansion(); }
				Test->TestTrue(TEXT("F2 expands details and preserves scene controls"), Client->IsObservationPanelExpanded()
					&& !Client->IsMoveInputIgnored() && !Client->IsLookInputIgnored() && !Client->bShowMouseCursor && !Client->IsObservationUIInputEnabled());
			}
			Test->TestTrue(TEXT("F2 preserves the already held movement key"), Controller.IsInputKeyDown(EKeys::W));
			Input(Controller, EKeys::MouseX, IE_Axis, 20.0f);
			Stage = 1;
			NextAction = Now + 1.0;
			return false;
		}
		if (Stage == 1)
		{
			AObservationPlayerController& Moving = *Target.Get();
			Test->TestTrue(TEXT("Enhanced Input moves the pawn while details remain open"), FVector::Dist(MovementStart, Moving.GetPawn()->GetActorLocation()) > 10.0);
			Test->TestTrue(TEXT("Mouse input turns the camera while details remain open"), FMath::Abs(FMath::FindDeltaAngleDegrees(YawStart, Moving.GetControlRotation().Yaw)) > 0.1f);
			const auto Observation = Moving.GetLocalPlayer()->GetSubsystem<ULocalPlayerObservationSubsystem>();
			Test->TestTrue(TEXT("Current state follows gameplay with details expanded"), FVector::Dist(MovementStart, Observation->GetPawnObservationSnapshot().Get()->GetCommon().Current.Pose.Value.GetLocalPosition(*Moving.GetWorld())) > 10.0);
			Input(Moving, EKeys::W, IE_Released, 0.0f);
			BeginTextInput(*Clients[InputClientIndex]);
			Stage = 11;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 11)
		{
			AObservationPlayerController& Controller = *Clients[InputClientIndex];
			UCharacterObservationPanel* Panel = Controller.GetObservationPanel();
			UEditableTextBox* Field = Panel->FaultInputs.FindChecked(TEXT("PktLag"));
			const FString Applied = Panel->Values.FindChecked(TEXT("PktLag_Applied"))->GetText().ToString();
			Click(Controller, Panel->FreezeButton->TakeWidget());
			Test->TestTrue(TEXT("A routed click freezes the snapshot"), Panel->bFrozen);
			Click(Controller, Field->TakeWidget());
			SelectAll(Controller);
			Type(Controller, TEXT("123"));
			Test->TestEqual(TEXT("Released Alt allows character input into the focused field"), Field->GetText().ToString(), FString(TEXT("123")));
			Press(Controller, EKeys::BackSpace);
			Character(Controller, TCHAR('\b'));
			Test->TestEqual(TEXT("Backspace edits the draft"), Field->GetText().ToString(), FString(TEXT("12")));
			Type(Controller, TEXT("4"));
			Test->TestEqual(TEXT("Typing continues after deletion"), Field->GetText().ToString(), FString(TEXT("124")));
			SelectAll(Controller);
			Type(Controller, TEXT("123"));
			Test->TestTrue(TEXT("Typing marks an unapplied draft"), Panel->bFaultInputsDirty);
			Test->TestEqual(TEXT("Typing does not apply network settings"), Panel->Values.FindChecked(TEXT("PktLag_Applied"))->GetText().ToString(), Applied);
			const float ScrollOffset = Panel->PageScrolls[1]->GetScrollOffset();
			Press(Controller, EKeys::LeftAlt);
			Test->TestFalse(TEXT("A second Alt press returns scene input from the text field"), Controller.IsObservationUIInputEnabled()
				|| Controller.bShowMouseCursor || Controller.IsMoveInputIgnored() || Controller.IsLookInputIgnored());
			Test->TestTrue(TEXT("Leaving editing preserves the frozen inspector, tab and draft"), Controller.IsObservationPanelExpanded() && Panel->bFrozen
				&& Field->GetText().ToString() == TEXT("123") && Panel->Pages->GetActiveWidgetIndex() == 1);
			Test->TestEqual(TEXT("Leaving editing preserves scroll position"), Panel->PageScrolls[1]->GetScrollOffset(), ScrollOffset);
			MovementStart = Controller.GetPawn()->GetActorLocation();
			Input(Controller, EKeys::W, IE_Pressed, 1.0f);
			Stage = 12;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 12)
		{
			AObservationPlayerController& Controller = *Clients[InputClientIndex];
			Test->TestTrue(TEXT("The pawn moves again after leaving editing"), FVector::Dist(MovementStart, Controller.GetPawn()->GetActorLocation()) > 10.0);
			Input(Controller, EKeys::W, IE_Released, 0.0f);
			Press(Controller, EKeys::LeftAlt);
			FSlateApplication::Get().SetUserFocus(Controller.GetLocalPlayer()->GetSlateUser()->GetUserIndex(), Controller.GetObservationPanel()->FaultInputs.FindChecked(TEXT("PktLag"))->TakeWidget());
			Press(Controller, EKeys::F2);
			Test->TestFalse(TEXT("F2 closes from a focused text field"), Controller.IsObservationPanelExpanded());
			Test->TestFalse(TEXT("Closing returns character controls"), Controller.IsMoveInputIgnored() || Controller.IsLookInputIgnored() || Controller.bShowMouseCursor);
			if (++InputClientIndex < Clients.Num())
			{
				BeginTextInput(*Clients[InputClientIndex]);
				Stage = 11;
				NextAction = Now + 0.5;
				return false;
			}
			AObservationPlayerController& Owner = *Target.Get();
			Focus(Owner);
			Press(Owner, EKeys::F2);
			Press(Owner, EKeys::LeftAlt);
			Owner.GetWorld()->GetWorldSettings()->SetPauserPlayerState(Owner.PlayerState);
			Owner.GetObservationPanel()->NativeTick(Owner.GetObservationPanel()->GetCachedGeometry(), 0.0f);
			Test->TestTrue(TEXT("Releasing Alt keeps editing active while the world is paused"), Owner.IsObservationUIInputEnabled());
			AObservationPlayerController* Other = Clients[0] == &Owner ? Clients[1] : Clients[0];
			Focus(*Other);
			Owner.GetObservationPanel()->NativeTick(Owner.GetObservationPanel()->GetCachedGeometry(), 0.0f);
			Test->TestFalse(TEXT("Switching clients releases the old inspector's input"), Owner.IsObservationUIInputEnabled() || Owner.IsMoveInputIgnored() || Owner.IsLookInputIgnored());
			Test->TestTrue(TEXT("Focus cleanup leaves the new client focused"), Other->HasObservationInputFocus());
			Owner.GetWorld()->GetWorldSettings()->SetPauserPlayerState(nullptr);
			Focus(Owner);
			Press(Owner, EKeys::LeftAlt);
			FSlateApplication::Get().OnApplicationActivationChanged(false);
			Test->TestFalse(TEXT("Application deactivation immediately releases editing"), Owner.IsObservationUIInputEnabled());
			Focus(Owner);
			Press(Owner, EKeys::F2);
			Stage = 100;
			return false;
		}
		// These stages run once; the existing popup and screenshot stages start below.
		if (Stage == 100)
		{
			AObservationPlayerController& Owner = *Target.Get();
			auto& Mapper = IPlatformInputDeviceMapper::Get();
			const FPlatformUserId User = FPlatformUserId::CreateFromInternalId(1);
			SharedInputDevice = Mapper.AllocateNewInputDeviceId();
			Mapper.Internal_MapInputDeviceToUser(SharedInputDevice, User, EInputDeviceConnectionState::Connected);
			FString Error;
			SharedLocalPlayer = Owner.GetGameInstance()->CreateLocalPlayer(User, Error, true);
			if (!SharedLocalPlayer.IsValid())
			{
				Test->AddError(FString::Printf(TEXT("Could not create the shared-viewport player: %s"), *Error));
				GEditor->RequestEndPlayMap();
				return true;
			}
			Stage = 101;
			return false;
		}
		if (Stage == 101)
		{
			AObservationPlayerController* Other = Cast<AObservationPlayerController>(SharedLocalPlayer->PlayerController);
			if (!Other || !Other->GetPawn() || !Other->GetObservationPanel() || !Other->GetObservationPanel()->IsInViewport()) { return false; }
			SharedController = Other;
			Stage = 102;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 102)
		{
			AObservationPlayerController& Owner = *Target.Get();
			AObservationPlayerController& Other = *SharedController.Get();
			UGameViewportClient* Viewport = Owner.GetWorld()->GetGameViewport();
			Test->TestEqual(TEXT("Both local players share one viewport"), Other.GetLocalPlayer()->ViewportClient.Get(), Viewport);
			Test->TestNotEqual(TEXT("The local players have separate Slate users"), Owner.GetLocalPlayer()->GetSlateUser()->GetUserIndex(), Other.GetLocalPlayer()->GetSlateUser()->GetUserIndex());
			Focus(Owner);
			Press(Owner, EKeys::F2);
			Press(Owner, EKeys::LeftAlt);
			FSlateApplication::Get().SetUserFocus(Other.GetLocalPlayer()->GetSlateUser()->GetUserIndex(), Viewport->GetGameViewportWidget());
			Test->TestTrue(TEXT("One inspector edits without ignoring viewport input"), Owner.IsObservationUIInputEnabled() && !Viewport->IgnoreInput());
			MovementStart = Owner.GetPawn()->GetActorLocation();
			YawStart = Owner.GetControlRotation().Yaw;
			SharedMovementStart = Other.GetPawn()->GetActorLocation();
			SharedYawStart = Other.GetControlRotation().Yaw;
			const auto OwnerDevice = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(Owner.GetPlatformUserId());
			ViewportInput(Owner, OwnerDevice, EKeys::W, IE_Pressed, 1.0f);
			ViewportInput(Owner, OwnerDevice, EKeys::MouseX, IE_Axis, 20.0f);
			ViewportInput(Owner, OwnerDevice, EKeys::SpaceBar, IE_Pressed, 1.0f);
			Test->TestFalse(TEXT("Editing consumes movement and jump keys before PlayerInput"), Owner.IsInputKeyDown(EKeys::W) || Owner.IsInputKeyDown(EKeys::SpaceBar));
			ViewportInput(Other, SharedInputDevice, EKeys::Gamepad_LeftY, IE_Axis, 1.0f);
			ViewportInput(Other, SharedInputDevice, EKeys::Gamepad_FaceButton_Bottom, IE_Pressed, 1.0f);
			Stage = 103;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 103)
		{
			AObservationPlayerController& Owner = *Target.Get();
			AObservationPlayerController& Other = *SharedController.Get();
			Test->TestTrue(TEXT("The editing player's pawn stays still"), FVector::Dist(MovementStart, Owner.GetPawn()->GetActorLocation()) < 1.0);
			Test->TestTrue(TEXT("The editing player's camera stays still"), FMath::Abs(FMath::FindDeltaAngleDegrees(YawStart, Owner.GetControlRotation().Yaw)) < 0.01f);
			Test->TestTrue(TEXT("Another local player still moves through the viewport"), FVector::Dist2D(SharedMovementStart, Other.GetPawn()->GetActorLocation()) > 10.0);
			Test->TestTrue(TEXT("Another local player still turns through the viewport"), FMath::Abs(FMath::FindDeltaAngleDegrees(SharedYawStart, Other.GetControlRotation().Yaw)) > 0.1f);
			Test->TestTrue(TEXT("Another local player still jumps through the viewport"), Other.GetPawn()->GetActorLocation().Z > SharedMovementStart.Z + 20.0);
			ViewportInput(Other, SharedInputDevice, EKeys::Gamepad_LeftY, IE_Axis, 0.0f);
			ViewportInput(Other, SharedInputDevice, EKeys::Gamepad_RightX, IE_Axis, 0.0f);
			ViewportInput(Other, SharedInputDevice, EKeys::Gamepad_FaceButton_Bottom, IE_Released, 0.0f);
			Press(Owner, EKeys::LeftAlt);
			Stage = 104;
			NextAction = Now + 0.2;
			return false;
		}
		if (Stage == 104)
		{
			AObservationPlayerController& Owner = *Target.Get();
			AObservationPlayerController& Other = *SharedController.Get();
			Press(Owner, EKeys::LeftAlt);
			Press(Other, EKeys::F2);
			Press(Other, EKeys::LeftAlt);
			Stage = 105;
			NextAction = Now + 0.2;
			return false;
		}
		if (Stage == 105)
		{
			AObservationPlayerController& Owner = *Target.Get();
			AObservationPlayerController& Other = *SharedController.Get();
			UGameViewportClient* Viewport = Owner.GetWorld()->GetGameViewport();
			Test->TestTrue(TEXT("Both local inspectors can edit together"), Owner.IsObservationUIInputEnabled() && Other.IsObservationUIInputEnabled());
			const auto OtherFocus = FSlateApplication::Get().GetUserFocusedWidget(Other.GetLocalPlayer()->GetSlateUser()->GetUserIndex());
			const EMouseCaptureMode CaptureMode = Viewport->GetMouseCaptureMode();
			Press(Owner, EKeys::LeftAlt);
			Press(Owner, EKeys::F2);
			Test->TestTrue(TEXT("Closing one inspector keeps the other in editing mode"), !Owner.IsObservationUIInputEnabled() && Other.IsObservationUIInputEnabled());
			Test->TestEqual(TEXT("Closing one inspector preserves shared mouse capture"), Viewport->GetMouseCaptureMode(), CaptureMode);
			Test->TestTrue(TEXT("Closing one inspector preserves the other Slate focus"), FSlateApplication::Get().GetUserFocusedWidget(Other.GetLocalPlayer()->GetSlateUser()->GetUserIndex()) == OtherFocus);
			Stage = 106;
			NextAction = Now + 0.2;
			return false;
		}
		if (Stage == 106)
		{
			AObservationPlayerController& Owner = *Target.Get();
			AObservationPlayerController& Other = *SharedController.Get();
			Test->TestTrue(TEXT("The remaining inspector retains focus across frames"), Other.IsObservationUIInputEnabled() && Other.HasObservationInputFocus());
			// Let Slate apply the restored focus before injecting a new gameplay press.
			MovementStart = Owner.GetPawn()->GetActorLocation();
			const auto OwnerDevice = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(Owner.GetPlatformUserId());
			ViewportInput(Owner, OwnerDevice, EKeys::W, IE_Pressed, 1.0f);
			Stage = 107;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 107)
		{
			AObservationPlayerController& Owner = *Target.Get();
			AObservationPlayerController& Other = *SharedController.Get();
			UGameViewportClient* Viewport = Owner.GetWorld()->GetGameViewport();
			Test->TestTrue(TEXT("The player who leaves editing receives game input"), Owner.IsInputKeyDown(EKeys::W));
			Test->TestTrue(TEXT("The player who leaves editing moves while the other inspector stays active"),
				FVector::Dist2D(MovementStart, Owner.GetPawn()->GetActorLocation()) > 10.0 && Other.IsObservationUIInputEnabled());
			const auto OwnerDevice = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(Owner.GetPlatformUserId());
			ViewportInput(Owner, OwnerDevice, EKeys::W, IE_Released, 0.0f);
			Press(Other, EKeys::LeftAlt);
			Press(Other, EKeys::F2);
			Test->TestFalse(TEXT("The last inspector releases editing"), Other.IsObservationUIInputEnabled());
			Test->TestEqual(TEXT("The last inspector restores game mouse capture"), Viewport->GetMouseCaptureMode(), EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown);
			const int32 SharedIdentifier = SharedLocalPlayer->GetConnectionIdentifier();
			Owner.GetGameInstance()->RemoveLocalPlayer(SharedLocalPlayer.Get());
			// Remove the server-side test pawn; its controller must keep the shared connection open until PIE ends.
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || World->GetNetMode() != NM_DedicatedServer) { continue; }
				for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				{
					APlayerController* PC = It->Get();
					if (PC && PC->NetPlayerIndex > 0 && PC->GetLocalPlayerConnectionIdentifier() == SharedIdentifier)
					{
						if (APawn* Pawn = PC->GetPawn()) { Pawn->Destroy(); }
						break;
					}
				}
			}
			SharedLocalPlayer.Reset();
			SharedController.Reset();
			if (FParse::Param(FCommandLine::Get(), TEXT("NullRHI")))
			{
				GEditor->RequestEndPlayMap();
				return true;
			}
			Stage = 30;
			NextAction = Now + 1.0;
			return false;
		}
		AObservationPlayerController* Controller = Target.Get();
		if (!Controller) { Test->AddError(TEXT("PIE target disappeared.")); GEditor->RequestEndPlayMap(); return true; }
		UCharacterObservationPanel* Panel = Controller->GetObservationPanel();
		const FIntPoint Sizes[] = {FIntPoint(640, 480), FIntPoint(1280, 720), FIntPoint(1920, 1080)};
		if (Stage == 30)
		{
			for (AObservationPlayerController* Client : Clients) { CheckProxyGhosts(*Client); }
			Focus(*Controller);
			Press(*Controller, EKeys::F2);
			Press(*Controller, EKeys::LeftAlt);
			Panel->SelectPage(2);
			Panel->HandleFreeze();
			Panel->RefreshTargets();
			for (const auto& Pawn : Panel->TargetPawns)
			{
				if (Pawn.IsValid() && Pawn->GetLocalRole() == ROLE_SimulatedProxy) { Remote = Cast<AObservationCharacter>(Pawn.Get()); break; }
			}
			if (!Remote.IsValid()) { Test->AddError(TEXT("No remote simulated character was available for selection.")); GEditor->RequestEndPlayMap(); return true; }
			RemoteOption = Panel->TargetSelector->GetOptionAtIndex(Panel->TargetPawns.IndexOfByPredicate([this](const auto& Pawn) { return Pawn.Get() == Remote.Get(); }));
			DraftBeforeSelection = Panel->FaultInputs.FindChecked(TEXT("PktLag"))->GetText().ToString();
			Stage = 36;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 36)
		{
			// The expanded panel needs a layout frame before routing a click by cached geometry.
			Click(*Controller, Panel->TargetSelector->TakeWidget());
			Stage = 31;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 31)
		{
			Test->TestTrue(TEXT("A routed click opens the target selector"), Panel->TargetSelector->IsOpen());
			Test->TestTrue(TEXT("Target popup retains owning-client input"), Controller->HasObservationInputFocus());
			PopupContent = FSlateApplication::Get().GetUserFocusedWidget(Controller->GetLocalPlayer()->GetSlateUser()->GetUserIndex());
			if (!PopupContent) { Test->AddError(TEXT("Target popup has no focused content.")); GEditor->RequestEndPlayMap(); return true; }
			CapturePopup(TEXT("640x480_TargetOptions"));
			const auto Option = FindOption(PopupContent.ToSharedRef(), RemoteOption);
			if (!Option) { Test->AddError(TEXT("Remote target option was not generated.")); GEditor->RequestEndPlayMap(); return true; }
			Click(*Controller, Option.ToSharedRef());
			Stage = 32;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 32)
		{
			const auto Observation = Controller->GetLocalPlayer()->GetSubsystem<ULocalPlayerObservationSubsystem>();
			Test->TestEqual(TEXT("A popup click selects the remote Pawn"), Observation->GetObservedPawn(), static_cast<APawn*>(Remote.Get()));
			Test->TestEqual(TEXT("Selected remote UI is sampled with simulated role"), Panel->LiveSnapshot.GetRole(), EObservedPawnRole::SimulatedProxy);
			Test->TestFalse(TEXT("Selecting a target releases the old frozen snapshot"), Panel->bFrozen);
			Test->TestEqual(TEXT("Unavailable Moves falls back to State"), Panel->Pages->GetActiveWidgetIndex(), 0);
			Test->TestEqual(TEXT("Real simulated UI hides Moves"), Panel->TabButtons[2]->GetVisibility(), ESlateVisibility::Collapsed);
			Test->TestEqual(TEXT("Real simulated UI hides prediction"), Panel->Cards[1]->GetVisibility(), ESlateVisibility::Collapsed);
			Test->TestEqual(TEXT("Real simulated UI hides ACK statistics"), Panel->PredictionStats->GetVisibility(), ESlateVisibility::Collapsed);
			Test->TestTrue(TEXT("Real simulated snapshot has a native reference"), Panel->LiveSnapshot.Get()->GetSimulated()->LastDecodedReference.IsAvailable());
			Test->TestFalse(TEXT("Real simulated snapshot exposes no autonomous predictions"), Panel->LiveSnapshot.Get()->GetAutonomous() != nullptr);
			Test->TestTrue(TEXT("Selecting remote does not copy its SavedMoves"), Panel->LiveSnapshot.Get()->GetAutonomous() == nullptr);
			Test->TestEqual(TEXT("Target selection preserves fault drafts"), Panel->FaultInputs.FindChecked(TEXT("PktLag"))->GetText().ToString(), DraftBeforeSelection);
			Test->TestEqual(TEXT("Fault controls retain the inspecting client's connection"), Panel->InputConnection.Get(), Controller->GetWorld()->GetNetDriver()->ServerConnection.Get());
			for (AObservationPlayerController* Client : Clients)
			{
				if (Client != Controller)
				{
					Test->TestEqual(TEXT("Remote selection is independent for each local player"), Client->GetLocalPlayer()->GetSubsystem<ULocalPlayerObservationSubsystem>()->GetObservedPawn(), static_cast<APawn*>(Client->GetPawn()));
					RemoteOwner = Client;
				}
			}
			Panel->HandleFreeze();
			FrozenRemoteSequence = Panel->DisplaySnapshot.Get()->GetPublicationSequence();
			ReferenceBeforeMovement = Panel->LiveSnapshot.Get()->GetSimulated()->LastDecodedReference.Value.ZeroOriginPosition;
			Stage = 33;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 33)
		{
			RemoteOwner->GetPawn()->AddMovementInput(FVector::ForwardVector, 0.5f);
			const FPawnObservationSnapshot Current = Controller->GetWorld()->GetSubsystem<UPawnObservationWorldSubsystem>()->GetLatest(*Remote.Get());
			if (FVector::Dist(Current.Get()->GetSimulated()->LastDecodedReference.Value.ZeroOriginPosition, ReferenceBeforeMovement) < 5.0) { return false; }
			Test->TestEqual(TEXT("Frozen simulated UI retains its snapshot"), Panel->DisplaySnapshot.Get()->GetPublicationSequence(), FrozenRemoteSequence);
			CheckProxyGhosts(*Controller);
			Test->TestTrue(TEXT("Remote ghosts continue moving while its UI snapshot is frozen"), Current.Get()->GetSimulated()->LastDecodedReference.Value.ZeroOriginPosition != ReferenceBeforeMovement);
			Panel->TargetSelector->SetSelectedIndex(0);
			Stage = 34;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 34)
		{
			const auto Observation = Controller->GetLocalPlayer()->GetSubsystem<ULocalPlayerObservationSubsystem>();
			Test->TestTrue(TEXT("Self option restores possession following"), Observation->IsFollowingControlledPawn());
			Test->TestEqual(TEXT("Self option returns to the autonomous Pawn"), Observation->GetObservedPawn(), static_cast<APawn*>(Controller->GetPawn()));
			Test->TestEqual(TEXT("Autonomous selection restores Moves"), Panel->TabButtons[2]->GetVisibility(), ESlateVisibility::Visible);
			Test->TestFalse(TEXT("Returning to self releases the frozen remote snapshot"), Panel->bFrozen);
			Click(*Controller, Panel->TargetSelector->TakeWidget());
			Stage = 35;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 35)
		{
			Test->TestTrue(TEXT("Target popup can reopen after selection"), Panel->TargetSelector->IsOpen());
			Press(*Controller, EKeys::F2);
			Test->TestFalse(TEXT("F2 closes the target popup and releases UI input"), Controller->IsObservationPanelExpanded()
				|| Controller->IsObservationUIInputEnabled() || Panel->TargetSelector->IsOpen() || Controller->IsMoveInputIgnored());
			PopupContent.Reset();
			Stage = 2;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 2)
		{
			if (SizeIndex < 2) { Controller->GetWorld()->GetGameViewport()->GetWindow()->Resize(FVector2D(Sizes[SizeIndex])); }
			Controller->SetObservationPanelExpanded(false);
			Stage = SizeIndex < 2 ? 20 : 3;
			NextAction = Now + 1.0;
			return false;
		}
		if (Stage == 20)
		{
			Focus(*Controller);
			Press(*Controller, EKeys::F2);
			Press(*Controller, EKeys::LeftAlt);
			Panel->SelectPage(1);
			Panel->PageScrolls[1]->ScrollWidgetIntoView(Panel->ScenarioSelector, false, EDescendantScrollDestination::Center);
			Stage = 21;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 21)
		{
			Click(*Controller, Panel->ScenarioSelector->TakeWidget());
			Stage = 22;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 22)
		{
			Test->TestTrue(TEXT("A routed click opens the scenario menu"), Panel->ScenarioSelector->IsOpen());
			Test->TestTrue(TEXT("The separate popup retains owning-player input"), Controller->HasObservationInputFocus());
			const auto Focused = FSlateApplication::Get().GetUserFocusedWidget(Controller->GetLocalPlayer()->GetSlateUser()->GetUserIndex());
			PopupContent = Focused;
			PopupWindow = Focused ? FSlateApplication::Get().FindWidgetWindow(Focused.ToSharedRef()) : nullptr;
			if (!PopupWindow) { Test->AddError(TEXT("Scenario popup has no focused Slate window.")); GEditor->RequestEndPlayMap(); return true; }
			CapturePopup(FString::Printf(TEXT("%dx%d_Combo_NormalSelected"), Sizes[SizeIndex].X, Sizes[SizeIndex].Y));
			const auto Hovered = FindOption(PopupContent.ToSharedRef(), TEXT("Latency spike"));
			if (!Hovered) { Test->AddError(TEXT("Scenario popup did not generate its option text.")); GEditor->RequestEndPlayMap(); return true; }
			Hover(*Controller, Hovered.ToSharedRef());
			Stage = 23;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 23)
		{
			CapturePopup(FString::Printf(TEXT("%dx%d_Combo_Hovered"), Sizes[SizeIndex].X, Sizes[SizeIndex].Y));
			const auto Option = FindOption(PopupContent.ToSharedRef(), TEXT("Short blackout"));
			if (!Option) { Test->AddError(TEXT("Scenario option to click was not found.")); GEditor->RequestEndPlayMap(); return true; }
			Click(*Controller, Option.ToSharedRef());
			Stage = 24;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 24)
		{
			Test->TestEqual(TEXT("Clicking a popup option selects the scenario"), Panel->ScenarioSelector->GetSelectedIndex(), 1);
			Test->TestFalse(TEXT("Selection closes the menu"), Panel->ScenarioSelector->IsOpen());
			Capture(*Panel, FString::Printf(TEXT("%dx%d_Combo_Collapsed"), Sizes[SizeIndex].X, Sizes[SizeIndex].Y), true);
			Click(*Controller, Panel->ScenarioSelector->TakeWidget());
			Stage = 25;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 25)
		{
			Test->TestTrue(TEXT("The menu can reopen after selecting an option"), Panel->ScenarioSelector->IsOpen());
			Press(*Controller, EKeys::LeftAlt);
			Test->TestFalse(TEXT("Alt leaves editing from the popup"), Controller->IsObservationUIInputEnabled());
			Test->TestFalse(TEXT("Leaving editing dismisses the owned popup"), Panel->ScenarioSelector->IsOpen());
			Test->TestTrue(TEXT("Alt retains the expanded sidebar"), Controller->IsObservationPanelExpanded());
			Stage = 26;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 26)
		{
			Press(*Controller, EKeys::LeftAlt);
			Stage = 28;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 28)
		{
			Click(*Controller, Panel->ScenarioSelector->TakeWidget());
			Stage = 27;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 27)
		{
			Test->TestTrue(TEXT("The menu opens again after entering editing"), Panel->ScenarioSelector->IsOpen());
			Press(*Controller, EKeys::F2);
			Test->TestFalse(TEXT("F2 closes from the separate popup"), Controller->IsObservationPanelExpanded() || Controller->IsObservationUIInputEnabled()
				|| Controller->IsMoveInputIgnored() || Controller->IsLookInputIgnored() || Panel->ScenarioSelector->IsOpen());
			PopupWindow.Reset();
			PopupContent.Reset();
			Stage = 3;
			NextAction = Now + 0.5;
			return false;
		}
		if (Stage == 3)
		{
			CheckBounds(*Controller, *Panel->HudSize);
			Capture(*Panel, FString::Printf(TEXT("%dx%d_HUD"), Sizes[SizeIndex].X, Sizes[SizeIndex].Y));
			Controller->SetObservationPanelExpanded(true);
			// Freeze a populated fixture so every saved-move row can be inspected at every viewport size.
			const FPawnObservationFrame& Live = *Panel->LiveSnapshot.Get();
			FAutonomousPawnObservation Auto = *Live.GetAutonomous();
			Auto.PredictionStatus = EObservationAvailability::Available;
			Auto.UnacknowledgedMoves.Reset();
			for (int32 Index = 0; Index < 8; ++Index)
			{
				FObservedSavedMove Move;
				Move.EndState = Live.GetCommon().Current;
				Move.MoveId = Auto.LastValidPair.PredictionBeforeResponse.MoveId;
				Move.MoveId.MoveSequence += Index + 1;
				Move.NativeTimeStamp = static_cast<float>(Live.GetCommon().SampleTime.GameTimeSeconds) - (7 - Index) * 0.016f;
				Move.DeltaTimeSeconds = 0.016f;
				Move.bBeforeTimeStampReset = Index == 0;
				Auto.UnacknowledgedMoves.Add(Move);
			}
			FObservedPawnRoleData Data; Data.Set<FAutonomousPawnObservation>(MoveTemp(Auto));
			Panel->HandleSnapshotUpdated(FPawnObservationSnapshot(FPawnObservationFrame(Live.GetCommon(), MoveTemp(Data), Live.GetSourceEpoch(), Live.GetPublicationSequence())));
			Panel->RefreshDisplay();
			Panel->HandleFreeze();
			Panel->SelectPage(0);
			Panel->PageScrolls[0]->SetScrollOffset(0.0f);
			PageIndex = 0;
			ScrollPart = 0;
			ScenarioIndex = 0;
			Stage = 4;
			NextAction = Now + 0.5;
			return false;
		}
		if (PageIndex == 3 && !Panel->bFrozen) { Panel->HandleFreeze(); }
		CheckBounds(*Controller, *Panel->DetailSize);
		if (PageIndex == 1)
		{
			for (UButton* Button : {Panel->ApplyButton.Get(), Panel->ScenarioStartButton.Get(), Panel->ScenarioStopButton.Get(), Panel->ResetButton.Get()}) { CheckBounds(*Controller, *Button); }
		}
		const FString PageName = PageIndex == 0 ? TEXT("State") : (PageIndex == 1 ? FString::Printf(TEXT("Faults_Scenario%d"), ScenarioIndex) : (PageIndex == 2 ? TEXT("Moves") : TEXT("SimulatedState")));
		Capture(*Panel, FString::Printf(TEXT("%dx%d_%s_%02d"), Sizes[SizeIndex].X, Sizes[SizeIndex].Y, *PageName, ScrollPart), true);
		UScrollBox* Scroll = Panel->PageScrolls[PageIndex == 3 ? 0 : PageIndex];
		const float Offset = Scroll->GetScrollOffset();
		const float End = Scroll->GetScrollOffsetOfEnd();
		if (Offset < End - 0.5f)
		{
			const float Step = FMath::Max(1.0f, static_cast<float>(Scroll->GetCachedGeometry().GetLocalSize().Y) * 0.8f);
			Scroll->SetScrollOffset(FMath::Min(End, Offset + Step));
			++ScrollPart;
			NextAction = Now + 0.25;
			return false;
		}
		Test->AddInfo(FString::Printf(TEXT("%dx%d %s covered through the final scroll position (%d captures)."), Sizes[SizeIndex].X, Sizes[SizeIndex].Y, *PageName, ScrollPart + 1));
		if (PageIndex == 1 && ++ScenarioIndex < 4)
		{
			Panel->ScenarioSelector->SetSelectedIndex(ScenarioIndex);
			Panel->UpdateScenarioControls();
			Scroll->SetScrollOffset(0.0f);
			ScrollPart = 0;
			NextAction = Now + 0.5;
			return false;
		}
		if (++PageIndex < 3)
		{
			Panel->SelectPage(PageIndex);
			if (PageIndex == 1)
			{
				Panel->ScenarioSelector->SetSelectedIndex(0);
				Panel->ScenarioRepeat->SetIsChecked(true);
				Panel->HandleScenarioRepeatChanged(true);
				if (Panel->AdvancedEditors->GetVisibility() == ESlateVisibility::Collapsed) { Panel->HandleAdvanced(); }
			}
			Panel->PageScrolls[PageIndex]->SetScrollOffset(0.0f);
			ScrollPart = 0;
			NextAction = Now + 0.5;
			return false;
		}
		if (PageIndex == 3)
		{
			Panel->RefreshTargets();
			const int32 Index = Panel->TargetPawns.IndexOfByPredicate([this](const auto& Pawn) { return Pawn.Get() == Remote.Get(); });
			Panel->TargetSelector->SetSelectedIndex(Index);
			Panel->SelectPage(0);
			Panel->PageScrolls[0]->SetScrollOffset(0.0f);
			ScrollPart = 0;
			NextAction = Now + 0.5;
			return false;
		}
		Panel->TargetSelector->SetSelectedIndex(0);
		if (++SizeIndex < UE_ARRAY_COUNT(Sizes)) { Stage = 2; NextAction = Now + 1.0; return false; }
		GEditor->RequestEndPlayMap();
		return true;
	}

private:
	FAutomationTestBase* Test;
	TStrongObjectPtr<ULevelEditorPlaySettings> Settings;
	TWeakObjectPtr<AObservationPlayerController> Target;
	TWeakObjectPtr<AObservationPlayerController> RemoteOwner;
	TWeakObjectPtr<AObservationCharacter> Remote;
	TWeakObjectPtr<ULocalPlayer> SharedLocalPlayer;
	TWeakObjectPtr<AObservationPlayerController> SharedController;
	FInputDeviceId SharedInputDevice;
	FVector SharedMovementStart = FVector::ZeroVector;
	float SharedYawStart = 0.0f;
	FString RemoteOption;
	FString DraftBeforeSelection;
	int64 FrozenRemoteSequence = 0;
	FVector ReferenceBeforeMovement = FVector::ZeroVector;
	TSharedPtr<SWindow> PopupWindow;
	TSharedPtr<SWidget> PopupContent;
	double Started = FPlatformTime::Seconds();
	double NextAction = 0.0;
	int32 Stage = 0;
	int32 SizeIndex = 0;
	int32 PageIndex = 0;
	int32 InputClientIndex = 0;
	int32 ScenarioIndex = 0;
	int32 ScrollPart = 0;
	FVector MovementStart = FVector::ZeroVector;
	float YawStart = 0.0f;

	inline void CheckProxyGhosts(AObservationPlayerController& Controller)
	{
		int32 Autonomous = 0, Simulated = 0;
		for (TActorIterator<AObservationCharacter> It(Controller.GetWorld()); It; ++It)
		{
			const FPawnObservationSnapshot Snapshot = Controller.GetWorld()->GetSubsystem<UPawnObservationWorldSubsystem>()->GetLatest(**It);
			if (!Test->TestTrue(TEXT("Proxy has shared world capture"), Snapshot.IsValid())) { continue; }
			const FAutonomousPawnObservation* Auto = Snapshot.Get()->GetAutonomous();
			const FSimulatedPawnObservation* Sim = Snapshot.Get()->GetSimulated();
			Autonomous += Auto != nullptr; Simulated += Sim != nullptr;
			const bool bPair = Auto && Auto->LastValidPair.IsAvailable();
			const bool bServer = bPair || (Sim && Sim->LastDecodedReference.IsAvailable());
			const bool bLocal = bPair || (Sim && Snapshot.Get()->GetCommon().Current.Pose.IsAvailable());
			Test->TestEqual(TEXT("Server ghost follows role source"), It->GetServerReferenceGhostMesh()->IsVisible(), bServer);
			Test->TestEqual(TEXT("Local ghost follows role source"), It->GetLocalStateGhostMesh()->IsVisible(), bLocal);
			if (Sim)
			{
				Test->TestTrue(TEXT("Real simulated proxy receives native references"), Sim->LastDecodedReference.IsAvailable());
				const FObservedPose& Pose = Snapshot.Get()->GetCommon().Current.Pose.Value;
				const FTransform Transform(Pose.Rotation, Pose.GetLocalPosition(*Controller.GetWorld()), It->GetActorScale3D());
				Test->TestTrue(TEXT("Simulated ghost follows capsule baseline"), It->GetLocalStateGhostMesh()->GetComponentLocation().Equals(Transform.TransformPosition(It->GetBaseTranslationOffset()), 0.01));
				if (Sim->LastDecodedReference.IsAvailable())
				{
					const FObservedPose& Ref = Sim->LastDecodedReference.Value;
					const FTransform Reference(Ref.Rotation, Ref.GetLocalPosition(*Controller.GetWorld()), It->GetActorScale3D());
					Test->TestTrue(TEXT("Simulated server ghost follows decoded reference"), It->GetServerReferenceGhostMesh()->GetComponentLocation().Equals(Reference.TransformPosition(It->GetBaseTranslationOffset()), 0.01));
				}
			}
		}
		Test->TestEqual(TEXT("Client has one autonomous character"), Autonomous, 1);
		Test->TestEqual(TEXT("Client has one simulated character"), Simulated, 1);
	}

	inline void Input(AObservationPlayerController& Controller, FKey Key, EInputEvent Event, float Amount)
	{
		const auto Device = IPlatformInputDeviceMapper::Get().GetPrimaryInputDeviceForUser(Controller.GetPlatformUserId());
		Controller.InputKey(FInputKeyEventArgs::CreateSimulated(Key, Event, Amount, -1, Device));
	}

	inline void ViewportInput(AObservationPlayerController& Controller, FInputDeviceId Device, FKey Key, EInputEvent Event, float Amount)
	{
		UGameViewportClient* Viewport = Controller.GetWorld()->GetGameViewport();
		auto Args = FInputKeyEventArgs::CreateSimulated(Key, Event, Amount, -1, Device);
		Args.Viewport = Viewport->Viewport;
		Args.DeltaTime = 1.0f / 60.0f;
		Args.NumSamples = 1;
		if (Key.IsAnalog()) { Viewport->InputAxis(Args); }
		else { Viewport->InputKey(Args); }
	}

	inline void Key(AObservationPlayerController& Controller, FKey InKey, bool bDown, const FModifierKeysState& Modifiers = FModifierKeysState(), bool bRepeat = false)
	{
		const FKeyEvent Event(InKey, Modifiers, Controller.GetLocalPlayer()->GetSlateUser()->GetUserIndex(), bRepeat, 0, 0);
		if (bDown) { FSlateApplication::Get().ProcessKeyDownEvent(Event); }
		else { FSlateApplication::Get().ProcessKeyUpEvent(Event); }
	}

	inline void Press(AObservationPlayerController& Controller, FKey InKey)
	{
		const FModifierKeysState Modifiers(false, false, false, false, InKey == EKeys::LeftAlt, false, false, false, false);
		Key(Controller, InKey, true, Modifiers);
		Key(Controller, InKey, false);
	}

	inline void SelectAll(AObservationPlayerController& Controller)
	{
		const FModifierKeysState Control(false, false, true, false, false, false, false, false, false);
		Key(Controller, EKeys::A, true, Control);
		Key(Controller, EKeys::A, false, Control);
	}

	inline void Character(AObservationPlayerController& Controller, TCHAR InCharacter)
	{
		FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(InCharacter, FModifierKeysState(), Controller.GetLocalPlayer()->GetSlateUser()->GetUserIndex(), false));
	}

	inline void Type(AObservationPlayerController& Controller, const FString& Text)
	{
		for (TCHAR InCharacter : Text) { Character(Controller, InCharacter); }
	}

	inline void BeginTextInput(AObservationPlayerController& Controller)
	{
		Focus(Controller);
		UCharacterObservationPanel* Panel = Controller.GetObservationPanel();
		Test->TestTrue(TEXT("The focused PIE client owns observation input"), Controller.HasObservationInputFocus());
		const FModifierKeysState Alt(false, false, false, false, true, false, false, false, false);
		Key(Controller, EKeys::LeftAlt, true, Alt);
		Key(Controller, EKeys::LeftAlt, true, Alt, true);
		Test->TestTrue(TEXT("Alt repeat does not toggle editing off"), Controller.IsObservationUIInputEnabled());
		Key(Controller, EKeys::LeftAlt, false);
		Controller.UpdateObservationUIInput();
		Test->TestTrue(TEXT("Releasing Alt retains editing and blocks scene input"), Controller.IsObservationUIInputEnabled()
			&& Controller.bShowMouseCursor && Controller.IsMoveInputIgnored() && Controller.IsLookInputIgnored());
		Panel->SelectPage(1);
		Panel->PageScrolls[1]->ScrollWidgetIntoView(Panel->FaultInputs.FindChecked(TEXT("PktLag")), false, EDescendantScrollDestination::Center);
	}

	inline void Hover(AObservationPlayerController& Controller, TSharedRef<SWidget> Widget)
	{
		auto& Slate = FSlateApplication::Get();
		const FVector2D Position = Widget->GetCachedGeometry().LocalToAbsolute(Widget->GetCachedGeometry().GetLocalSize() * 0.5);
		const FVector2D Previous = Slate.GetCursorPos();
		Slate.SetCursorPos(Position);
		Slate.ProcessMouseMoveEvent(FPointerEvent(Controller.GetLocalPlayer()->GetSlateUser()->GetUserIndex(), FSlateApplication::CursorPointerIndex, Position, Previous, TSet<FKey>(), EKeys::Invalid, 0.0f, FModifierKeysState()));
	}

	inline void Click(AObservationPlayerController& Controller, TSharedRef<SWidget> Widget)
	{
		auto& Slate = FSlateApplication::Get();
		Hover(Controller, Widget);
		const auto Window = Slate.FindWidgetWindow(Widget);
		if (!Window) { Test->AddError(TEXT("Click target has no Slate window.")); return; }
		const FVector2D Position = Slate.GetCursorPos();
		const uint32 UserIndex = Controller.GetLocalPlayer()->GetSlateUser()->GetUserIndex();
		Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), FPointerEvent(UserIndex, FSlateApplication::CursorPointerIndex, Position, Position, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0.0f, FModifierKeysState()));
		Slate.ProcessMouseButtonUpEvent(FPointerEvent(UserIndex, FSlateApplication::CursorPointerIndex, Position, Position, TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, FModifierKeysState()));
	}

	inline TSharedPtr<SWidget> FindOption(TSharedRef<SWidget> Widget, const FString& Text)
	{
		// Type contract (caller guaranteed): the native Slate tree reports STextBlock only for actual STextBlock
		// instances; custom widgets must not spoof that type name. The name check precedes this downcast.
		if (Widget->GetType() == TEXT("STextBlock") && StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString() == Text) { return Widget; }
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (auto Found = FindOption(Children->GetChildAt(Index), Text)) { return Found; }
		}
		return nullptr;
	}

	inline void Focus(AObservationPlayerController& Controller)
	{
		auto& Slate = FSlateApplication::Get();
		const auto Viewport = Controller.GetWorld()->GetGameViewport();
		Viewport->GetWindow()->MoveWindowTo(FVector2D(30.0, 30.0));
		Viewport->GetWindow()->BringToFront(true);
		Slate.OnApplicationActivationChanged(true);
		Slate.ProcessWindowActivatedEvent(FWindowActivateEvent(FWindowActivateEvent::EA_Activate, Viewport->GetWindow().ToSharedRef()));
		Slate.SetUserFocus(Controller.GetLocalPlayer()->GetSlateUser()->GetUserIndex(), Viewport->GetGameViewportWidget());
		Controller.SetInputMode(FInputModeGameOnly());
	}

	inline void CheckBounds(AObservationPlayerController& Controller, UWidget& Widget)
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("NullRHI"))) { return; }
		const FGeometry Screen = UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(&Controller);
		const FGeometry Geometry = Widget.GetCachedGeometry();
		const FVector2D TopLeft = Screen.AbsoluteToLocal(Geometry.GetAbsolutePosition());
		const FVector2D BottomRight = Screen.AbsoluteToLocal(Geometry.LocalToAbsolute(Geometry.GetLocalSize()));
		Test->TestTrue(*FString::Printf(TEXT("%s has visible area inside its player's screen"), *Widget.GetName()),
			Geometry.GetLocalSize().X > 0.0 && Geometry.GetLocalSize().Y > 0.0
			&& TopLeft.X >= -1.0 && TopLeft.Y >= -1.0 && BottomRight.X <= Screen.GetLocalSize().X + 1.0 && BottomRight.Y <= Screen.GetLocalSize().Y + 1.0);
	}

	inline void Capture(UCharacterObservationPanel& Panel, const FString& Name, bool bDetail = false)
	{
		TArray<FColor> Pixels;
		FIntVector Size;
		if (SizeIndex == 2)
		{
			// The desktop can be smaller than 1920x1080. Render the same native widget to an exact-size target.
			FVector2D DrawSize(1920.0, 1080.0);
			const float Scale = UWidgetLayoutLibrary::GetViewportScale(&Panel);
			Panel.NativeTick(FGeometry::MakeRoot(DrawSize / Scale, FSlateLayoutTransform(Scale)), 0.0f);
			Panel.ForceLayoutPrepass();
			if (bDetail) { DrawSize = FVector2D(Panel.DetailSize->GetWidthOverride(), Panel.DetailSize->GetHeightOverride()); }
			FWidgetRenderer Renderer(true);
			// Slate applies display gamma; keep the target linear to avoid another sRGB conversion.
			TStrongObjectPtr<UTextureRenderTarget2D> TargetTexture(FWidgetRenderer::CreateTargetFor(DrawSize, TF_Bilinear, false));
			Renderer.DrawWidget(TargetTexture.Get(), bDetail ? Panel.DetailSize->TakeWidget() : Panel.TakeWidget(), bDetail ? 1.0f : Scale, DrawSize, 0.0f);
			// Slate already renders gamma-corrected colors into this target.
			FReadSurfaceDataFlags ReadFlags;
			ReadFlags.SetLinearToGamma(false);
			if (!Test->TestTrue(TEXT("1920x1080 render target captured"), TargetTexture->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, ReadFlags))) { return; }
			Size = FIntVector(TargetTexture->SizeX, TargetTexture->SizeY, 0);
			Test->TestEqual(TEXT("Render target width"), Size.X, static_cast<int32>(DrawSize.X));
			Test->TestEqual(TEXT("Render target height"), Size.Y, static_cast<int32>(DrawSize.Y));
		}
		else if (!Test->TestTrue(TEXT("Rendered inspector screenshot captured"), FSlateApplication::Get().TakeScreenshot(bDetail ? Panel.DetailSize->TakeWidget() : Panel.TakeWidget(), Pixels, Size))) { return; }
		SaveCapture(Pixels, Size, Name);
	}

	inline void CapturePopup(const FString& Name)
	{
		TArray<FColor> Pixels;
		FIntVector Size;
		if (Test->TestTrue(TEXT("The native popup screenshot captured"), FSlateApplication::Get().TakeScreenshot(PopupContent.ToSharedRef(), Pixels, Size))) { SaveCapture(Pixels, Size, Name); }
	}

	inline void SaveCapture(const TArray<FColor>& Pixels, const FIntVector& Size, const FString& Name)
	{
		Test->AddInfo(FString::Printf(TEXT("%s rendered at %dx%d"), *Name, Size.X, Size.Y));
		TArray64<uint8> PNG;
		FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Automation/ObservationRoles/Screenshots");
		IFileManager::Get().MakeDirectory(*Directory, true);
		Test->TestTrue(TEXT("Rendered screenshot saved"), FFileHelper::SaveArrayToFile(PNG, *(Directory / (Name + TEXT(".png")))));
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObservationInspectorTest, "NetReconcile.Observation.InspectorPIE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FObservationInspectorTest::RunTest(const FString&)
{
	if (GEditor->PlayWorld) { AddError(TEXT("End the current PIE session before running the inspector test.")); return false; }
	ADD_LATENT_AUTOMATION_COMMAND(FObservationInspectorCommand(this));
	return true;
}

#endif
