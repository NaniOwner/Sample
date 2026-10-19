#include "SessionLobbySubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

const FString USessionLobbySubsystem::LobbyMap = TEXT("/Game/Variant_Shooter/SessionLevel");
const FString USessionLobbySubsystem::MatchMap = TEXT("/Game/Variant_Shooter/Lvl_Shooter");
namespace
{
	const FName RoomNameKey(TEXT("ROOM_NAME"));
	const FName ProjectKey(TEXT("PROJECT_LOBBY"));
	const FString ProjectValue(TEXT("Sample_Shooter_v1"));
}

void USessionLobbySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Status = TEXT("방을 생성하거나 검색해 참가하세요. (LAN)");
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::OnNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::OnTravelFailure);
	}
}

void USessionLobbySubsystem::Deinitialize()
{
	if (Sessions.IsValid())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		Sessions->ClearOnStartSessionCompleteDelegate_Handle(StartHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	}
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	Sessions.Reset();
	Super::Deinitialize();
}

bool USessionLobbySubsystem::EnsureSessions()
{
	if (!Sessions.IsValid())
	{
		// World-aware lookup isolates sessions between PIE game instances.
		Sessions = Online::GetSessionInterface(GetWorld());
		if (!Sessions.IsValid())
		{
			SetStatus(TEXT("세션 서비스를 사용할 수 없습니다."));
			return false;
		}
		CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnCreated));
		FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::OnFound));
		JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnJoined));
		StartHandle = Sessions->AddOnStartSessionCompleteDelegate_Handle(FOnStartSessionCompleteDelegate::CreateUObject(this, &ThisClass::OnStarted));
		DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::OnDestroyed));
	}
	return true;
}

void USessionLobbySubsystem::SetStatus(const FString& Message, bool bIsBusy)
{
	Status = Message;
	bBusy = bIsBusy;
	UE_LOG(LogTemp, Display, TEXT("Lobby: %s"), *Message);
	OnLobbyChanged.Broadcast();
}

bool USessionLobbySubsystem::IsInRoom() const
{
	// PIE can connect a client before it has joined an advertised online session.
	return (Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr) ||
		(GetWorld() && GetWorld()->GetNetMode() == NM_Client);
}

bool USessionLobbySubsystem::IsRoomHost() const
{
	return bHosting && IsInRoom() && GetWorld() && GetWorld()->GetNetMode() == NM_ListenServer;
}

int32 USessionLobbySubsystem::GetRoomCapacity() const
{
	const FNamedOnlineSession* Session = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	return Session ? Session->SessionSettings.NumPublicConnections : 4;
}

void USessionLobbySubsystem::NotifyLobbyReady()
{
	bStartingMatch = false;
	SetStatus(IsRoomHost() ? TEXT("참가자를 기다리는 중입니다. 방장이 게임을 시작할 수 있습니다.") :
		IsInRoom() ? TEXT("방에 참가했습니다. 방장의 시작을 기다리세요.") :
		GetWorld() && GetWorld()->GetNetMode() == NM_ListenServer ? TEXT("리슨 서버로 실행 중입니다. 방 생성 버튼으로 방을 개설하세요.") :
		TEXT("방을 생성하거나 검색해 참가하세요. (LAN)"));
}

void USessionLobbySubsystem::HostRoom(const FString& RoomName, int32 Capacity)
{
	if (bBusy || !EnsureSessions()) return;
	if (IsInRoom())
	{
		SetStatus(TEXT("기존 방에서 나간 뒤 새 방을 생성하세요."));
		return;
	}
	if (!GetWorld() || (GetWorld()->GetNetMode() != NM_Standalone && GetWorld()->GetNetMode() != NM_ListenServer))
	{
		SetStatus(TEXT("방 생성은 독립 실행 창 또는 리슨 서버에서 할 수 있습니다."));
		return;
	}
	CurrentRoomName = RoomName.TrimStartAndEnd().Left(40);
	if (CurrentRoomName.IsEmpty()) CurrentRoomName = TEXT("새로운 방");
	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = true;
	Settings.NumPublicConnections = FMath::Clamp(Capacity, 2, 16);
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = false;
	Settings.bUsesPresence = false;
	Settings.Set(RoomNameKey, CurrentRoomName, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(ProjectKey, ProjectValue, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	SetStatus(TEXT("방을 생성하는 중..."), true);
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings))
	{
		SetStatus(TEXT("방 생성 요청에 실패했습니다."));
	}
}

void USessionLobbySubsystem::OnCreated(FName SessionName, bool bSuccess)
{
	if (SessionName != NAME_GameSession) return;
	if (!bSuccess)
	{
		SetStatus(TEXT("방 생성에 실패했습니다."));
		return;
	}
	bHosting = true;
	if (GetWorld() && GetWorld()->GetNetMode() == NM_ListenServer)
	{
		// Reuse PIE's listener so automatically connected clients are not disconnected
		// by reopening the same map. Register those players in the advertised room.
		if (AGameStateBase* GS = GetWorld()->GetGameState())
		{
			for (APlayerState* Player : GS->PlayerArray)
			{
				if (IsValid(Player) && Player->GetUniqueId().IsValid())
				{
					Sessions->RegisterPlayer(SessionName, *Player->GetUniqueId().GetUniqueNetId(), false);
				}
			}
		}
		UE_LOG(LogTemp, Display, TEXT("Lobby: Created room using existing listen server; connected players preserved."));
		NotifyLobbyReady();
		return;
	}
	SetStatus(TEXT("리슨 서버 로비를 여는 중..."), true);
	UGameplayStatics::OpenLevel(this, FName(*LobbyMap), true, TEXT("listen"));
}

void USessionLobbySubsystem::FindRooms()
{
	if (bBusy || !EnsureSessions()) return;
	if (IsInRoom()) return;
	if (!GetWorld() || GetWorld()->GetNetMode() != NM_Standalone)
	{
		SetStatus(TEXT("방 검색은 방 나가기 후 독립 실행 상태에서 할 수 있습니다."));
		return;
	}
	Rooms.Empty();
	SearchResults.Empty();
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = true;
	Search->MaxSearchResults = 100;
	SetStatus(TEXT("LAN 방을 검색하는 중..."), true);
	if (!Sessions->FindSessions(0, Search.ToSharedRef())) SetStatus(TEXT("방 검색 요청에 실패했습니다."));
}

void USessionLobbySubsystem::OnFound(bool bSuccess)
{
	if (bLeaving) return;
	if (bSuccess && Search.IsValid())
	{
		for (const FOnlineSessionSearchResult& Result : Search->SearchResults)
		{
			FString Project;
			Result.Session.SessionSettings.Get(ProjectKey, Project);
			if (Project != ProjectValue || Result.Session.NumOpenPublicConnections <= 0) continue;
			FSessionRoomInfo Room;
			Result.Session.SessionSettings.Get(RoomNameKey, Room.Name);
			Room.Capacity = Result.Session.SessionSettings.NumPublicConnections;
			Room.Players = Room.Capacity - Result.Session.NumOpenPublicConnections;
			Room.Ping = Result.PingInMs;
			Rooms.Add(Room);
			SearchResults.Add(Result);
		}
	}
	SetStatus(bSuccess ? FString::Printf(TEXT("검색 완료: %d개의 방"), Rooms.Num()) : TEXT("방 검색에 실패했습니다."));
}

void USessionLobbySubsystem::JoinRoom(int32 RoomIndex)
{
	if (bBusy || !EnsureSessions() || IsInRoom()) return;
	if (!GetWorld() || GetWorld()->GetNetMode() != NM_Standalone) return;
	if (!SearchResults.IsValidIndex(RoomIndex))
	{
		SetStatus(TEXT("참가할 방을 선택하세요."));
		return;
	}
	CurrentRoomName = Rooms[RoomIndex].Name;
	SetStatus(TEXT("방에 참가하는 중..."), true);
	if (!Sessions->JoinSession(0, NAME_GameSession, SearchResults[RoomIndex])) SetStatus(TEXT("방 참가 요청에 실패했습니다."));
}

void USessionLobbySubsystem::OnJoined(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	if (SessionName != NAME_GameSession) return;
	FString Address;
	APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();
	if (Result != EOnJoinSessionCompleteResult::Success || !PC || !Sessions->GetResolvedConnectString(SessionName, Address))
	{
		SetStatus(TEXT("방에 참가하지 못했습니다. 방 목록을 새로 검색하세요."));
		if (IsInRoom()) LeaveRoom();
		return;
	}
	bHosting = false;
	PC->ClientTravel(Address, TRAVEL_Absolute);
}

void USessionLobbySubsystem::StartMatch()
{
	if (bBusy || bStartingMatch) return;
	if (!IsRoomHost())
	{
		SetStatus(TEXT("게임 시작은 방장만 할 수 있습니다."));
		return;
	}
	if (!GetWorld()->GetMapName().EndsWith(TEXT("SessionLevel")) || !FPackageName::DoesPackageExist(MatchMap))
	{
		SetStatus(TEXT("로비 또는 전투 맵 설정을 확인하세요."));
		return;
	}
	bStartingMatch = true;
	SetStatus(TEXT("게임을 시작하는 중..."), true);
	if (!Sessions->StartSession(NAME_GameSession))
	{
		bStartingMatch = false;
		SetStatus(TEXT("게임 시작 요청에 실패했습니다."));
	}
}

void USessionLobbySubsystem::OnStarted(FName SessionName, bool bSuccess)
{
	if (SessionName != NAME_GameSession) return;
	if (!bSuccess || !IsRoomHost())
	{
		bStartingMatch = false;
		SetStatus(TEXT("게임 시작에 실패했습니다."));
		return;
	}
	UWorld* World = GetWorld();
	AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
	if (!GameMode)
	{
		bStartingMatch = false;
		SetStatus(TEXT("서버 게임 모드를 찾을 수 없습니다."));
		return;
	}
	GameMode->bUseSeamlessTravel = true;
#if WITH_EDITOR
	// PIE otherwise falls back to a full reload, which drops the existing
	// listener/connection instead of carrying all players to the new map.
	if (World->WorldType == EWorldType::PIE)
	{
		if (IConsoleVariable* AllowTravel = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel")))
		{
			AllowTravel->Set(1, ECVF_SetByCode);
		}
	}
#endif
	UE_LOG(LogTemp, Display, TEXT("Lobby: Seamless ServerTravel to %s; players=%d port=%d"),
		*MatchMap, World->GetGameState() ? World->GetGameState()->PlayerArray.Num() : 0, World->URL.Port);
	if (!World->ServerTravel(MatchMap, true))
	{
		bStartingMatch = false;
		SetStatus(TEXT("전투 맵 이동에 실패했습니다."));
	}
}

void USessionLobbySubsystem::LeaveRoom()
{
	if (bLeaving) return;
	bLeaving = true;
	SetStatus(TEXT("방에서 나가는 중..."), true);
	if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession))
	{
		ReturnToLobby();
	}
	else if (!Sessions->DestroySession(NAME_GameSession))
	{
		bLeaving = false;
		SetStatus(TEXT("방 종료에 실패했습니다. 다시 시도하세요."));
	}
}

void USessionLobbySubsystem::OnDestroyed(FName SessionName, bool bSuccess)
{
	if (SessionName != NAME_GameSession || !bLeaving) return;
	if (bSuccess) ReturnToLobby();
	else
	{
		bLeaving = false;
		SetStatus(TEXT("방 종료에 실패했습니다. 다시 시도하세요."));
	}
}

void USessionLobbySubsystem::ReturnToLobby()
{
	bHosting = bLeaving = bStartingMatch = false;
	CurrentRoomName.Empty();
	Rooms.Empty();
	SearchResults.Empty();
	SetStatus(TEXT("방을 생성하거나 검색해 참가하세요. (LAN)"));
	UGameplayStatics::OpenLevel(this, FName(*LobbyMap), true);
}

void USessionLobbySubsystem::OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error)
{
	if (!World || World->GetGameInstance() != GetGameInstance() || bLeaving) return;
	UE_LOG(LogTemp, Warning, TEXT("Lobby: Network failure: %s"), *Error);
	LeaveRoom();
}

void USessionLobbySubsystem::OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error)
{
	if (!World || World->GetGameInstance() != GetGameInstance() || bLeaving) return;
	UE_LOG(LogTemp, Warning, TEXT("Lobby: Travel failure: %s"), *Error);
	LeaveRoom();
}
