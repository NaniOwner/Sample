#include "SessionLobbyWidget.h"
#include "SessionLobbySubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"

USessionLobbySubsystem* USessionLobbyWidget::GetLobby() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<USessionLobbySubsystem>() : nullptr;
}

UButton* USessionLobbyWidget::AddButton(UVerticalBox* Box, const FString& Label)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>();
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetText(FText::FromString(Label));
	Button->AddChild(Text);
	Box->AddChildToVerticalBox(Button)->SetPadding(FMargin(0, 6));
	Button->SetBackgroundColor(FLinearColor(0.16f, 0.2f, 0.26f));
	return Button;
}

TSharedRef<SWidget> USessionLobbyWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UBorder* Background = WidgetTree->ConstructWidget<UBorder>();
		Background->SetBrushColor(FLinearColor(0.025f, 0.03f, 0.045f));
		Background->SetHorizontalAlignment(HAlign_Center);
		Background->SetVerticalAlignment(VAlign_Center);
		WidgetTree->RootWidget = Background;
		USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
		Size->SetWidthOverride(640.0f);
		Background->AddChild(Size);
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>();
		Size->AddChild(Box);
		UTextBlock* Title = WidgetTree->ConstructWidget<UTextBlock>();
		Title->SetText(FText::FromString(TEXT("멀티플레이 로비")));
		FSlateFontInfo Font = Title->GetFont();
		Font.Size = 30;
		Title->SetFont(Font);
		Box->AddChildToVerticalBox(Title)->SetPadding(FMargin(0, 0, 0, 20));
		RoomName = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("RoomName"));
		RoomName->SetHintText(FText::FromString(TEXT("방 이름 (최대 4명)")));
		RoomName->SetForegroundColor(FLinearColor(0.05f, 0.05f, 0.05f));
		Box->AddChildToVerticalBox(RoomName);
		HostButton = AddButton(Box, TEXT("방 생성"));
		SearchButton = AddButton(Box, TEXT("방 검색 / 새로고침"));
		RoomList = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("RoomList"));
		Box->AddChildToVerticalBox(RoomList)->SetPadding(FMargin(0, 6));
		JoinButton = AddButton(Box, TEXT("선택한 방 참가"));
		PlayersText = WidgetTree->ConstructWidget<UTextBlock>();
		PlayersText->SetAutoWrapText(true);
		Box->AddChildToVerticalBox(PlayersText)->SetPadding(FMargin(0, 14));
		StartButton = AddButton(Box, TEXT("게임 시작 (방장)"));
		LeaveButton = AddButton(Box, TEXT("방 나가기"));
		StatusText = WidgetTree->ConstructWidget<UTextBlock>();
		StatusText->SetAutoWrapText(true);
		Box->AddChildToVerticalBox(StatusText)->SetPadding(FMargin(0, 14));
		HostButton->OnClicked.AddDynamic(this, &ThisClass::Host);
		SearchButton->OnClicked.AddDynamic(this, &ThisClass::SearchRooms);
		JoinButton->OnClicked.AddDynamic(this, &ThisClass::Join);
		StartButton->OnClicked.AddDynamic(this, &ThisClass::Start);
		LeaveButton->OnClicked.AddDynamic(this, &ThisClass::Leave);
	}
	return Super::RebuildWidget();
}

void USessionLobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetIsFocusable(true);
	if (GetLobby()) GetLobby()->OnLobbyChanged.AddUniqueDynamic(this, &ThisClass::Refresh);
	Refresh();
}

void USessionLobbyWidget::NativeDestruct()
{
	if (GetLobby()) GetLobby()->OnLobbyChanged.RemoveDynamic(this, &ThisClass::Refresh);
	Super::NativeDestruct();
}

void USessionLobbyWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	RefreshTime += DeltaTime;
	if (RefreshTime >= 0.5f)
	{
		RefreshTime = 0.0f;
		Refresh();
	}
}

void USessionLobbyWidget::Refresh()
{
	USessionLobbySubsystem* Lobby = GetLobby();
	if (!Lobby || !RoomList) return;
	const bool InRoom = Lobby->IsInRoom();
	const bool CanBrowse = !InRoom && GetWorld()->GetNetMode() == NM_Standalone;
	HostButton->SetIsEnabled(!InRoom && !Lobby->bBusy);
	SearchButton->SetIsEnabled(CanBrowse && !Lobby->bBusy);
	RoomName->SetIsEnabled(!InRoom && !Lobby->bBusy);
	RoomList->SetIsEnabled(CanBrowse && !Lobby->bBusy);
	JoinButton->SetIsEnabled(CanBrowse && !Lobby->bBusy && !Lobby->Rooms.IsEmpty());
	StartButton->SetVisibility(Lobby->IsRoomHost() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	StartButton->SetIsEnabled(!Lobby->bBusy);
	LeaveButton->SetVisibility(InRoom || GetWorld()->GetNetMode() != NM_Standalone ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	LeaveButton->SetIsEnabled(!Lobby->bBusy);
	StatusText->SetText(FText::FromString(Lobby->Status));
	TArray<FString> Options;
	for (int32 Index = 0; Index < Lobby->Rooms.Num(); ++Index)
	{
		const FSessionRoomInfo& Room = Lobby->Rooms[Index];
		Options.Add(FString::Printf(TEXT("%d. %s   %d/%d명   %dms"), Index + 1, *Room.Name, Room.Players, Room.Capacity, Room.Ping));
	}
	bool Changed = RoomList->GetOptionCount() != Options.Num();
	for (int32 Index = 0; !Changed && Index < Options.Num(); ++Index) Changed = RoomList->GetOptionAtIndex(Index) != Options[Index];
	if (Changed)
	{
		RoomList->ClearOptions();
		for (const FString& Option : Options) RoomList->AddOption(Option);
		if (!Options.IsEmpty()) RoomList->SetSelectedIndex(0);
	}
	FString Players;
	if (InRoom)
	{
		Players = (Lobby->CurrentRoomName.IsEmpty() ? FString(TEXT("연결된 로비")) : Lobby->CurrentRoomName) + TEXT("\n참가자:\n");
		if (AGameStateBase* GS = GetWorld()->GetGameState())
		{
			for (APlayerState* PS : GS->PlayerArray)
			{
				if (IsValid(PS)) Players += PS->GetPlayerName() + TEXT("\n");
			}
		}
	}
	PlayersText->SetText(FText::FromString(Players));
}

void USessionLobbyWidget::Host() { if (GetLobby()) GetLobby()->HostRoom(RoomName->GetText().ToString()); }
void USessionLobbyWidget::SearchRooms() { if (GetLobby()) GetLobby()->FindRooms(); }
void USessionLobbyWidget::Join() { if (GetLobby()) GetLobby()->JoinRoom(RoomList->GetSelectedIndex()); }
void USessionLobbyWidget::Start() { if (GetLobby()) GetLobby()->StartMatch(); }
void USessionLobbyWidget::Leave() { if (GetLobby()) GetLobby()->LeaveRoom(); }
