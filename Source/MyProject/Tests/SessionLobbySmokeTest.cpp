#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Session/SessionLobbySubsystem.h"
#include "ShooterCharacter.h"

// Development-only, two-process smoke check. Uses the same subsystem as the menu.
// -ExecCmds="Lobby.Smoke Host" and -ExecCmds="Lobby.Smoke Client"
static FAutoConsoleCommandWithWorldAndArgs LobbySmokeCommand(
	TEXT("Lobby.Smoke"), TEXT("Run a Host or Client lobby smoke test in a standalone game."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InitialWorld)
	{
		if (!InitialWorld || !InitialWorld->GetGameInstance() || Args.Num() != 1) return;
		const bool bHost = Args[0].Equals(TEXT("Host"), ESearchCase::IgnoreCase);
		TWeakObjectPtr<UGameInstance> Instance = InitialWorld->GetGameInstance();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[Instance, bHost, Stage = 0, StartedAt = FPlatformTime::Seconds(), FinishedTime = -1.0](float DeltaTime) mutable
			{
				const double Elapsed = FPlatformTime::Seconds() - StartedAt;
				if (!Instance.IsValid()) return false;
				UWorld* World = Instance->GetWorld();
				USessionLobbySubsystem* Lobby = Instance->GetSubsystem<USessionLobbySubsystem>();
				if (!World || !Lobby || !World->GetFirstPlayerController()) return true;
				if (Elapsed > 120.0f)
				{
					UE_LOG(LogTemp, Error, TEXT("LOBBY_SMOKE FAIL role=%s stage=%d status=%s"), bHost ? TEXT("Host") : TEXT("Client"), Stage, *Lobby->Status);
					if (World->WorldType != EWorldType::PIE) FPlatformMisc::RequestExitWithStatus(false, 1);
					return false;
				}
				if (Stage == 0)
				{
					Stage = 1;
					if (bHost) Lobby->HostRoom(TEXT("SmokeTestRoom"), 4);
					else Lobby->FindRooms();
				}
				if (!bHost && Stage == 1 && !Lobby->bBusy)
				{
					if (Lobby->Rooms.IsEmpty())
					{
						Lobby->FindRooms();
					}
					else
					{
						UE_LOG(LogTemp, Display, TEXT("LOBBY_SMOKE FOUND rooms=%d name=%s"), Lobby->Rooms.Num(), *Lobby->Rooms[0].Name);
						Stage = 2;
						Lobby->JoinRoom(0);
					}
				}
				const int32 Players = World->GetGameState() ? World->GetGameState()->PlayerArray.Num() : 0;
				if (World->GetMapName().EndsWith(TEXT("SessionLevel")) && Players >= 2)
				{
					if (bHost && Stage == 1 && Lobby->IsRoomHost() && !Lobby->bBusy)
					{
						Stage = 2;
						UE_LOG(LogTemp, Display, TEXT("LOBBY_SMOKE START players=%d netmode=%d"), Players, int32(World->GetNetMode()));
						// Give the client time to finish its lobby load and replication.
					}
					if (!bHost && Stage == 2)
					{
						Stage = 3;
						Lobby->StartMatch();
						UE_LOG(LogTemp, Display, TEXT("LOBBY_SMOKE CLIENT_START_REJECTED busy=%d"), Lobby->bBusy);
					}
				}
				if (bHost && Stage == 2)
				{
					if (FinishedTime < 0.0f) FinishedTime = Elapsed;
					if (Elapsed - FinishedTime > 4.0f)
					{
						Stage = 3;
						FinishedTime = -1.0f;
						Lobby->StartMatch();
					}
				}
				if (World->GetMapName().EndsWith(TEXT("Lvl_Shooter")) && Players >= 2 && Stage < 4)
				{
					APlayerController* PC = Instance->GetFirstLocalPlayerController();
					if (PC && PC->GetPawn() && PC->GetPawn()->IsA<AShooterCharacter>() &&
						((bHost && World->GetNetMode() == NM_ListenServer) || (!bHost && World->GetNetMode() == NM_Client)))
					{
						if (bHost && World->WorldType == EWorldType::PIE)
						{
							int32 ReadyClients = 0;
							for (const FWorldContext& Context : GEngine->GetWorldContexts())
							{
								UWorld* ClientWorld = Context.World();
								if (!ClientWorld || Context.WorldType != EWorldType::PIE || ClientWorld->GetNetMode() != NM_Client) continue;
								APlayerController* ClientPC = ClientWorld->GetGameInstance()->GetFirstLocalPlayerController();
								if (ClientWorld->GetMapName().EndsWith(TEXT("Lvl_Shooter")) && ClientPC && ClientPC->GetPawn() &&
									ClientPC->GetPawn()->IsA<AShooterCharacter>()) ++ReadyClients;
							}
							if (ReadyClients < Players - 1) return true;
							UE_LOG(LogTemp, Display, TEXT("LOBBY_SMOKE PIE_CLIENTS_PASS clients=%d map=Lvl_Shooter pawn=ShooterCharacter"), ReadyClients);
						}
						Stage = 4;
						FinishedTime = Elapsed;
						UE_LOG(LogTemp, Display, TEXT("LOBBY_SMOKE PASS role=%s map=%s players=%d pawn=%s netmode=%d"), bHost ? TEXT("Host") : TEXT("Client"), *World->GetMapName(), Players, *PC->GetPawn()->GetClass()->GetName(), int32(World->GetNetMode()));
					}
				}
				if (Stage == 4 && Elapsed - FinishedTime > (bHost ? 15.0f : 5.0f))
				{
					if (World->WorldType != EWorldType::PIE) FPlatformMisc::RequestExitWithStatus(false, 0);
					return false;
				}
				return true;
			}), 0.25f);
	}));

#endif
