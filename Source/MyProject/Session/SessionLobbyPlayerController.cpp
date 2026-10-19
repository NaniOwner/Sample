#include "SessionLobbyPlayerController.h"
#include "SessionLobbySubsystem.h"
#include "SessionLobbyWidget.h"
#include "Engine/GameInstance.h"

void ASessionLobbyPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;
	GetGameInstance()->GetSubsystem<USessionLobbySubsystem>()->NotifyLobbyReady();
	LobbyWidget = CreateWidget<USessionLobbyWidget>(this, USessionLobbyWidget::StaticClass());
	if (LobbyWidget)
	{
		LobbyWidget->AddToViewport();
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(LobbyWidget->TakeWidget());
		SetInputMode(Mode);
		bShowMouseCursor = true;
	}
}

void ASessionLobbyPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (LobbyWidget) LobbyWidget->RemoveFromParent();
	Super::EndPlay(Reason);
}
