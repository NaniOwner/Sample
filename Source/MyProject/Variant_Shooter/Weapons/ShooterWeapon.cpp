// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterWeapon.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "ShooterProjectile.h"
#include "ShooterWeaponHolder.h"
#include "Components/SceneComponent.h"
#include "TimerManager.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "ShooterCharacter.h"
#include "Net/UnrealNetwork.h"

AShooterWeapon::AShooterWeapon()
{
	bReplicates = true;
	bNetUseOwnerRelevancy = true;
	PrimaryActorTick.bCanEverTick = true;

	// create the root
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// create the first person mesh
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(RootComponent);

	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
	FirstPersonMesh->bOnlyOwnerSee = true;

	// create the third person mesh
	ThirdPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Third Person Mesh"));
	ThirdPersonMesh->SetupAttachment(RootComponent);

	ThirdPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	ThirdPersonMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::WorldSpaceRepresentation);
	ThirdPersonMesh->bOwnerNoSee = true;
}

void AShooterWeapon::BeginPlay()
{
	Super::BeginPlay();
	InitializeOwner();
}

void AShooterWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShooterWeapon, bActive);
}

void AShooterWeapon::OnRep_Owner()
{
	Super::OnRep_Owner();
	InitializeOwner();
}

void AShooterWeapon::InitializeOwner()
{
	WeaponOwner = Cast<IShooterWeaponHolder>(GetOwner());
	PawnOwner = Cast<APawn>(GetOwner());
	if (WeaponOwner && IsValid(PawnOwner))
	{
		GetOwner()->OnDestroyed.AddUniqueDynamic(this, &AShooterWeapon::OnOwnerDestroyed);
		WeaponOwner->AttachWeaponMeshes(this);
		OnRep_Active();
	}
}

void AShooterWeapon::OnRep_Active()
{
	SetActorHiddenInGame(!bActive);
	if (WeaponOwner && bActive)
	{
		WeaponOwner->OnWeaponActivated(this);
	}
}

void AShooterWeapon::MulticastFireEffects_Implementation()
{
	if (WeaponOwner && GetNetMode() != NM_DedicatedServer)
	{
		WeaponOwner->PlayFiringMontage(FiringMontage);
		WeaponOwner->AddWeaponRecoil(FiringRecoil);
	}
}

void AShooterWeapon::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::OnOwnerDestroyed(AActor* DestroyedActor)
{
	// ensure this weapon is destroyed when the owner is destroyed
	if (HasAuthority())
	{
		Destroy();
	}
}

void AShooterWeapon::ActivateWeapon(const FName& OwnerTag)
{
	if (!HasAuthority())
	{
		return;
	}
	bActive = true;
	// save the owner tag for perception noise detection
	NoiseOwnerTag = OwnerTag;

	// unhide this weapon
	SetActorHiddenInGame(false);

	// notify the owner
	WeaponOwner->OnWeaponActivated(this);
	ForceNetUpdate();
}

void AShooterWeapon::DeactivateWeapon()
{
	if (!HasAuthority())
	{
		return;
	}
	bActive = false;
	// ensure we're no longer firing this weapon while deactivated
	StopFiring();

	// hide the weapon
	SetActorHiddenInGame(true);

	// notify the owner
	WeaponOwner->OnWeaponDeactivated(this);
	ForceNetUpdate();
}

void AShooterWeapon::StartFiring()
{
	if (!HasAuthority() || !WeaponOwner || !IsValid(PawnOwner))
	{
		return;
	}
	// raise the firing flag
	bIsFiring = true;

	// check how much time has passed since we last shot
	// this may be under the refire rate if the weapon shoots slow enough and the player is spamming the trigger
	const float TimeSinceLastShot = GetWorld()->GetTimeSeconds() - TimeOfLastShot;

	if (TimeSinceLastShot > RefireRate)
	{
		// fire the weapon right away
		Fire();

	} else {

		// if we're full auto, schedule the next shot
		if (bFullAuto)
		{
			GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, FMath::Max(0.01f, RefireRate - TimeSinceLastShot), false);
		}

	}
}

void AShooterWeapon::StopFiring()
{
	// lower the firing flag
	bIsFiring = false;

	// clear the refire timer
	GetWorld()->GetTimerManager().ClearTimer(RefireTimer);
}

void AShooterWeapon::Fire()
{
	// ensure the player still wants to fire. They may have let go of the trigger
	if (!HasAuthority() || !bIsFiring || !WeaponOwner || !IsValid(PawnOwner))
	{
		return;
	}
	if (const AShooterCharacter* Shooter = Cast<AShooterCharacter>(PawnOwner))
	{
		if (Shooter->IsDead())
		{
			StopFiring();
			return;
		}
	}
	
	// fire a projectile at the target
	FireProjectile(WeaponOwner->GetWeaponTargetLocation());
	if (!bIsFiring)
	{
		return;
	}

	// update the time of our last shot
	TimeOfLastShot = GetWorld()->GetTimeSeconds();

	// make noise so the AI perception system can hear us
	MakeNoise(ShotLoudness, PawnOwner, PawnOwner->GetActorLocation(), ShotNoiseRange, NoiseOwnerTag);

	// are we full auto?
	if (bFullAuto)
	{
		// schedule the next shot
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::Fire, FMath::Max(0.01f, RefireRate), false);
	} else {

		// for semi-auto weapons, schedule the cooldown notification
		GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &AShooterWeapon::FireCooldownExpired, FMath::Max(0.01f, RefireRate), false);

	}
}

void AShooterWeapon::FireCooldownExpired()
{
	// notify the owner
	WeaponOwner->OnSemiWeaponRefire();
}

void AShooterWeapon::FireProjectile(const FVector& TargetLocation)
{
	if (!HasAuthority() || !WeaponOwner || !ProjectileClass)
	{
		return;
	}
	AShooterCharacter* Shooter = Cast<AShooterCharacter>(PawnOwner);
	if (Shooter && !Shooter->CanSpendHealthForShot())
	{
		StopFiring();
		return;
	}
	// get the projectile transform
	FTransform ProjectileTransform = CalculateProjectileSpawnTransform(TargetLocation);
	
	// spawn the projectile
	AShooterProjectile* Projectile = GetWorld()->SpawnActorDeferred<AShooterProjectile>(
		ProjectileClass, ProjectileTransform, GetOwner(), PawnOwner,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn, ESpawnActorScaleMethod::OverrideRootScale);
	if (!Projectile)
	{
		return;
	}
	if (Shooter && !Shooter->TrySpendHealthForShot())
	{
		Projectile->Destroy();
		StopFiring();
		return;
	}
	Projectile->SetNoiseTag(NoiseOwnerTag);
	Projectile->SetHitHealthRestore(Shooter ? Shooter->GetHitHealthRestore() : 0.0f);
	Projectile->FinishSpawning(ProjectileTransform, false, nullptr, ESpawnActorScaleMethod::OverrideRootScale);
	MulticastFireEffects();
}

FTransform AShooterWeapon::CalculateProjectileSpawnTransform(const FVector& TargetLocation) const
{
	// find the muzzle location
	const FVector MuzzleLoc = PawnOwner && PawnOwner->IsPlayerControlled()
		? PawnOwner->GetPawnViewLocation()
		: FirstPersonMesh->GetSocketLocation(MuzzleSocketName);

	// calculate the spawn location ahead of the muzzle
	const FVector SpawnLoc = MuzzleLoc + ((TargetLocation - MuzzleLoc).GetSafeNormal() * MuzzleOffset);

	// find the aim rotation vector while applying some variance to the target 
	const FRotator AimRot = UKismetMathLibrary::FindLookAtRotation(SpawnLoc, TargetLocation + (UKismetMathLibrary::RandomUnitVector() * AimVariance));

	// return the built transform
	return FTransform(AimRot, SpawnLoc, FVector::OneVector);
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetFirstPersonAnimInstanceClass() const
{
	return FirstPersonAnimInstanceClass;
}

const TSubclassOf<UAnimInstance>& AShooterWeapon::GetThirdPersonAnimInstanceClass() const
{
	return ThirdPersonAnimInstanceClass;
}
