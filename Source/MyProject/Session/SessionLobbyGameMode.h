#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SessionLobbyGameMode.generated.h"

/** SessionLevel is a waiting room; gameplay pawns spawn in Lvl_Shooter. */
UCLASS()
class MYPROJECT_API ASessionLobbyGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ASessionLobbyGameMode();
	virtual void PreLogin(const FString& Options, const FString& Address,
		const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
};
