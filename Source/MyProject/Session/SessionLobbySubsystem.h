#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Engine/EngineBaseTypes.h"
#include "SessionLobbySubsystem.generated.h"

USTRUCT(BlueprintType)
struct FSessionRoomInfo
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) int32 Players = 0;
	UPROPERTY(BlueprintReadOnly) int32 Capacity = 0;
	UPROPERTY(BlueprintReadOnly) int32 Ping = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSessionLobbyChanged);

/** Session data survives the lobby reload and the transition to gameplay. */
UCLASS()
class MYPROJECT_API USessionLobbySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category="Lobby") void HostRoom(const FString& RoomName, int32 Capacity = 4);
	UFUNCTION(BlueprintCallable, Category="Lobby") void FindRooms();
	UFUNCTION(BlueprintCallable, Category="Lobby") void JoinRoom(int32 RoomIndex);
	UFUNCTION(BlueprintCallable, Category="Lobby") void StartMatch();
	UFUNCTION(BlueprintCallable, Category="Lobby") void LeaveRoom();
	UFUNCTION(BlueprintPure, Category="Lobby") bool IsInRoom() const;
	UFUNCTION(BlueprintPure, Category="Lobby") bool IsRoomHost() const;
	int32 GetRoomCapacity() const;
	void NotifyLobbyReady();
	bool IsStartingMatch() const { return bStartingMatch; }

	UPROPERTY(BlueprintReadOnly, Category="Lobby") TArray<FSessionRoomInfo> Rooms;
	UPROPERTY(BlueprintReadOnly, Category="Lobby") FString Status;
	UPROPERTY(BlueprintReadOnly, Category="Lobby") FString CurrentRoomName;
	UPROPERTY(BlueprintReadOnly, Category="Lobby") bool bBusy = false;
	UPROPERTY(BlueprintAssignable, Category="Lobby") FSessionLobbyChanged OnLobbyChanged;

	static const FString LobbyMap;
	static const FString MatchMap;
private:
	IOnlineSessionPtr Sessions;
	TSharedPtr<FOnlineSessionSearch> Search;
	TArray<FOnlineSessionSearchResult> SearchResults;
	FDelegateHandle CreateHandle, FindHandle, JoinHandle, StartHandle, DestroyHandle;
	FDelegateHandle NetworkFailureHandle, TravelFailureHandle;
	bool bHosting = false;
	bool bLeaving = false;
	bool bStartingMatch = false;
	bool EnsureSessions();
	void SetStatus(const FString& Message, bool bIsBusy = false);
	void OnCreated(FName SessionName, bool bSuccess);
	void OnFound(bool bSuccess);
	void OnJoined(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void OnStarted(FName SessionName, bool bSuccess);
	void OnDestroyed(FName SessionName, bool bSuccess);
	void ReturnToLobby();
	void OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Error);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type Type, const FString& Error);
};
