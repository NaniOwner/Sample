#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "SessionLobbyPlayerController.generated.h"

class USessionLobbyWidget;

UCLASS()
class MYPROJECT_API ASessionLobbyPlayerController : public APlayerController
{
	GENERATED_BODY()
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(Transient) TObjectPtr<USessionLobbyWidget> LobbyWidget;
};
