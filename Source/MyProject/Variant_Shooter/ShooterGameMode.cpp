// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterGameMode.h"
#include "ShooterUI.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerStart.h"
#include "ShooterPlayerController.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Components/StaticMeshComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AShooterGameMode::AShooterGameMode()
{
	static ConstructorHelpers::FClassFinder<AActor> PackClass(TEXT("/Game/Object/BP_HealthPack"));
	HealthPackClass = PackClass.Class;
}

void AShooterGameMode::BeginPlay()
{
	Super::BeginPlay();
	CheckHealthPacks();
	GetWorldTimerManager().SetTimer(HealthPackCheckTimer, this, &AShooterGameMode::CheckHealthPacks, 0.25f, true);

	// create the UI
	APlayerController* LocalController = UGameplayStatics::GetPlayerController(GetWorld(), 0);
	if (LocalController && LocalController->IsLocalController() && ShooterUIClass &&
		(ShooterUI = CreateWidget<UShooterUI>(LocalController, ShooterUIClass)))
	{
		ShooterUI->AddToViewport(0);
	}

	// create each additional local player.
	// Player 0 will be created automatically as part of regular game init
	for (int32 i = 2; i <= NumberOfLocalPlayers; ++i)
	{
		if (AShooterPlayerController* NewPlayer = Cast<AShooterPlayerController>(UGameplayStatics::CreatePlayer(GetWorld(), -1, true)))
		{
			NewPlayer->SetTeam(1 - (i % 2));
		}
	}
}

void AShooterGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(HealthPackCheckTimer);
	GetWorldTimerManager().ClearTimer(HealthPackRespawnTimer);
	Super::EndPlay(EndPlayReason);
}

bool AShooterGameMode::HasHealthPack() const
{
	if (HealthPackClass)
	{
		for (TActorIterator<AActor> It(GetWorld(), HealthPackClass); It; ++It)
		{
			if (IsValid(*It) && !It->IsActorBeingDestroyed())
			{
				return true;
			}
		}
	}
	return false;
}

void AShooterGameMode::CheckHealthPacks()
{
	if (!HealthPackClass || !HasAuthority())
	{
		return;
	}
	if (HasHealthPack())
	{
		GetWorldTimerManager().ClearTimer(HealthPackRespawnTimer);
		for (TActorIterator<AActor> It(GetWorld(), HealthPackClass); It; ++It)
		{
			It->OnDestroyed.AddUniqueDynamic(this, &AShooterGameMode::OnHealthPackDestroyed);
		}
	}
	else if (!GetWorldTimerManager().IsTimerActive(HealthPackRespawnTimer))
	{
		GetWorldTimerManager().SetTimer(HealthPackRespawnTimer, this,
			&AShooterGameMode::SpawnHealthPack, FMath::Max(0.1f, HealthPackRespawnDelay), false);
	}
}

void AShooterGameMode::OnHealthPackDestroyed(AActor* DestroyedActor)
{
	CheckHealthPacks();
}

bool AShooterGameMode::FindHealthPackFloor(FVector& OutLocation) const
{
	TArray<AActor*> Starts;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), Starts);
	if (Starts.IsEmpty())
	{
		return false;
	}
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FCollisionObjectQueryParams FloorObjects(ECC_WorldStatic);
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(HealthPackFloor), false);
	for (int32 Attempt = 0; Attempt < 64; ++Attempt)
	{
		const FVector Origin = Starts[FMath::RandHelper(Starts.Num())]->GetActorLocation();
		FVector Candidate;
		FNavLocation NavLocation;
		if (Navigation && Navigation->GetRandomReachablePointInRadius(Origin, HealthPackSpawnRadius, NavLocation))
		{
			Candidate = NavLocation.Location;
		}
		else
		{
			// Maps without navigation still use floor traces near their player starts.
			const float Angle = FMath::FRandRange(0.0f, 2.0f * PI);
			const float Radius = FMath::Sqrt(FMath::FRand()) * HealthPackSpawnRadius;
			Candidate = Origin + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0f);
		}
		FHitResult Floor;
		if (!GetWorld()->LineTraceSingleByObjectType(Floor, Candidate + FVector(0, 0, 150),
			Candidate - FVector(0, 0, 1000), FloorObjects, QueryParams) || Floor.ImpactNormal.Z < 0.85f)
		{
			continue;
		}
		// Reserve room above the floor, including clearance from players and props.
		if (!GetWorld()->OverlapBlockingTestByChannel(Floor.ImpactPoint + FVector(0, 0, 52),
			FQuat::Identity, ECC_Pawn, FCollisionShape::MakeBox(FVector(40, 40, 50)), QueryParams))
		{
			OutLocation = Floor.ImpactPoint;
			return true;
		}
	}
	return false;
}

void AShooterGameMode::SpawnHealthPack()
{
	if (!HasAuthority() || !HealthPackClass || HasHealthPack())
	{
		return;
	}
	FVector FloorLocation;
	if (!FindHealthPackFloor(FloorLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("Health pack: no free floor position found; retrying after respawn delay."));
		return;
	}
	const FTransform SpawnTransform(FRotator::ZeroRotator, FloorLocation + FVector(0, 0, 52));
	AActor* Pack = GetWorld()->SpawnActorDeferred<AActor>(HealthPackClass, SpawnTransform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
	if (!Pack)
	{
		return;
	}
	Pack->SetReplicates(true);
	Pack->SetReplicateMovement(true);
	Pack->OnDestroyed.AddUniqueDynamic(this, &AShooterGameMode::OnHealthPackDestroyed);
	UGameplayStatics::FinishSpawningActor(Pack, SpawnTransform);
	if (!IsValid(Pack))
	{
		return;
	}
	// The Blueprint pivot may be centered on its mesh. Place the visual bottom on the floor.
	FBox MeshBounds(ForceInit);
	TInlineComponentArray<UStaticMeshComponent*> Meshes(Pack);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetStaticMesh() && Mesh->IsVisible())
		{
			MeshBounds += Mesh->Bounds.GetBox();
		}
	}
	if (MeshBounds.IsValid)
	{
		Pack->AddActorWorldOffset(FVector(0, 0, FloorLocation.Z + 2.0f - MeshBounds.Min.Z));
	}
	Pack->ForceNetUpdate();
}

AActor* AShooterGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	// build the current player tag
	FName PlayerTag = FName(*FString::Printf(TEXT("Player%d"), CurrentPlayerStartAssignment));

	// find all player starts with the matching player tag
	TArray<AActor*> PlayerStarts;

	UGameplayStatics::GetAllActorsOfClassWithTag(GetWorld(), APlayerStart::StaticClass(), PlayerTag, PlayerStarts);

	// increment the player start assignment index
	++CurrentPlayerStartAssignment;

	// if no PlayerStarts were found, default to all PlayerStarts instead
	if (PlayerStarts.IsEmpty())
	{
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), PlayerStarts);
	}

	// have we found at least one PlayerStart?
	if (!PlayerStarts.IsEmpty())
	{
		return PlayerStarts[ FMath::RandRange(0, PlayerStarts.Num() - 1) ];
	}

	// no PlayerStarts in the level
	return nullptr;
}

void AShooterGameMode::IncrementTeamScore(uint8 TeamByte)
{
	// retrieve the team score if any
	int32 Score = 0;
	if (int32* FoundScore = TeamScores.Find(TeamByte))
	{
		Score = *FoundScore;
	}

	// increment the score for the given team
	++Score;
	TeamScores.Add(TeamByte, Score);

	// update the UI
	if (ShooterUI)
	{
		ShooterUI->BP_UpdateScore(TeamByte, Score);
	}
}

bool AShooterGameMode::ShouldSpawnEnemyNPCs() const
{
	// only spawn enemy NPCs in single player mode
	return NumberOfLocalPlayers < 2;
}
