#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SessionLobbyWidget.generated.h"

class UButton;
class UTextBlock;
class UComboBoxString;
class UEditableTextBox;
class UVerticalBox;
class USessionLobbySubsystem;

/** Native menu is ready to use without editing or replacing existing Blueprint assets. */
UCLASS()
class MYPROJECT_API USessionLobbyWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
private:
	UPROPERTY() TObjectPtr<UEditableTextBox> RoomName;
	UPROPERTY() TObjectPtr<UComboBoxString> RoomList;
	UPROPERTY() TObjectPtr<UTextBlock> StatusText;
	UPROPERTY() TObjectPtr<UTextBlock> PlayersText;
	UPROPERTY() TObjectPtr<UButton> HostButton;
	UPROPERTY() TObjectPtr<UButton> SearchButton;
	UPROPERTY() TObjectPtr<UButton> JoinButton;
	UPROPERTY() TObjectPtr<UButton> StartButton;
	UPROPERTY() TObjectPtr<UButton> LeaveButton;
	float RefreshTime = 0.0f;
	USessionLobbySubsystem* GetLobby() const;
	UButton* AddButton(UVerticalBox* Box, const FString& Label);
	UFUNCTION() void Refresh();
	UFUNCTION() void Host();
	UFUNCTION() void SearchRooms();
	UFUNCTION() void Join();
	UFUNCTION() void Start();
	UFUNCTION() void Leave();
};
