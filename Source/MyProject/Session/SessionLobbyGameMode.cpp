#include "SessionLobbyGameMode.h"
#include "SessionLobbyPlayerController.h"
#include "SessionLobbySubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/GameStateBase.h"

ASessionLobbyGameMode::ASessionLobbyGameMode()
{
	PlayerControllerClass = ASessionLobbyPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	bStartPlayersAsSpectators = true;
	bUseSeamlessTravel = true;
}

void ASessionLobbyGameMode::PreLogin(const FString& Options, const FString& Address,
	const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	const USessionLobbySubsystem* Lobby = GetGameInstance()->GetSubsystem<USessionLobbySubsystem>();
	if (ErrorMessage.IsEmpty() && Lobby)
	{
		if (Lobby->IsStartingMatch()) ErrorMessage = TEXT("The host has started the match.");
		else if (GameState && GameState->PlayerArray.Num() >= Lobby->GetRoomCapacity()) ErrorMessage = TEXT("The room is full.");
	}
}
