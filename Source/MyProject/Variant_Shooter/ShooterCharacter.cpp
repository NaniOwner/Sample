// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "EnhancedInputComponent.h"
#include "Components/InputComponent.h"
#include "Components/PawnNoiseEmitterComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "ShooterGameMode.h"
#include "Net/UnrealNetwork.h"

AShooterCharacter::AShooterCharacter()
{
	bReplicates = true;
	// create the noise emitter component
	PawnNoiseEmitter = CreateDefaultSubobject<UPawnNoiseEmitterComponent>(TEXT("Pawn Noise Emitter"));

	// configure movement
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 600.0f, 0.0f);
}

void AShooterCharacter::BeginPlay()
{
	if (HasAuthority())
	{
		CurrentHP = FMath::Max(1.0f, MaxHP);
	}
	Super::BeginPlay();
	OnRep_CurrentHP();
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShooterCharacter, CurrentHP);
	DOREPLIFETIME(AShooterCharacter, bDead);
	DOREPLIFETIME(AShooterCharacter, CurrentWeapon);
}

float AShooterCharacter::GetHealthPercent() const
{
	return FMath::Clamp(CurrentHP / FMath::Max(1.0f, MaxHP), 0.0f, 1.0f);
}

void AShooterCharacter::OnRep_CurrentHP()
{
	OnDamaged.Broadcast(GetHealthPercent());
}

void AShooterCharacter::OnRep_Dead()
{
	if (bDead)
	{
		ApplyDeathPresentation();
	}
}

void AShooterCharacter::OnRep_CurrentWeapon()
{
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->InitializeOwner();
		OnWeaponActivated(CurrentWeapon);
	}
}

void AShooterCharacter::RefreshWeaponHUD()
{
	// Retained for existing HUD bindings; weapons no longer use ammunition.
}

bool AShooterCharacter::CanSpendHealthForShot() const
{
	return !IsDead() && FMath::IsFinite(ShotHealthCost) &&
		CurrentHP > FMath::Max(0.0f, ShotHealthCost);
}

bool AShooterCharacter::TrySpendHealthForShot()
{
	if (!HasAuthority() || !CanSpendHealthForShot())
	{
		return false;
	}
	CurrentHP -= FMath::Max(0.0f, ShotHealthCost);
	OnRep_CurrentHP();
	ForceNetUpdate();
	return true;
}

float AShooterCharacter::RestoreHealth(float Amount)
{
	if (!HasAuthority() || IsDead() || !FMath::IsFinite(Amount) || Amount <= 0.0f)
	{
		return 0.0f;
	}
	const float PreviousHP = CurrentHP;
	CurrentHP = FMath::Min(FMath::Max(1.0f, MaxHP), CurrentHP + Amount);
	if (CurrentHP > PreviousHP)
	{
		OnRep_CurrentHP();
		ForceNetUpdate();
	}
	return CurrentHP - PreviousHP;
}

void AShooterCharacter::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the respawn timer
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
}

void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// base class handles move, aim and jump inputs
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Firing
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AShooterCharacter::DoStartFiring);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoStopFiring);

		// Switch weapon
		EnhancedInputComponent->BindAction(SwitchWeaponAction, ETriggerEvent::Triggered, this, &AShooterCharacter::DoSwitchWeapon);
	}

}

float AShooterCharacter::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// ignore if already dead
	if (!HasAuthority() || IsDead() || !FMath::IsFinite(Damage) || Damage <= 0.0f)
	{
		return 0.0f;
	}

	// Reduce HP
	const float AppliedDamage = FMath::Min(CurrentHP, Damage);
	CurrentHP = FMath::Max(0.0f, CurrentHP - AppliedDamage);

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		Die();
	}

	// update the HUD
	OnRep_CurrentHP();
	ForceNetUpdate();

	return AppliedDamage;
}

void AShooterCharacter::DoAim(float Yaw, float Pitch)
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoAim(Yaw, Pitch);
	}
}

void AShooterCharacter::DoMove(float Right, float Forward)
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoMove(Right, Forward);
	}
}

void AShooterCharacter::DoJumpStart()
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoJumpStart();
	}
}

void AShooterCharacter::DoJumpEnd()
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoJumpEnd();
	}
}

void AShooterCharacter::DoStartFiring()
{
	if (!HasAuthority())
	{
		ServerStartFiring();
		return;
	}
	// fire the current weapon
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StartFiring();
	}
}

void AShooterCharacter::DoStopFiring()
{
	if (!HasAuthority())
	{
		ServerStopFiring();
		return;
	}
	// stop firing the current weapon
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StopFiring();
	}
}

void AShooterCharacter::DoSwitchWeapon()
{
	if (!HasAuthority())
	{
		ServerSwitchWeapon();
		return;
	}
	// ensure we have at least two weapons two switch between
	if (OwnedWeapons.Num() > 1 && !IsDead())
	{
		// deactivate the old weapon
		CurrentWeapon->DeactivateWeapon();

		// find the index of the current weapon in the owned list
		int32 WeaponIndex = OwnedWeapons.Find(CurrentWeapon);

		// is this the last weapon?
		if (WeaponIndex == OwnedWeapons.Num() - 1)
		{
			// loop back to the beginning of the array
			WeaponIndex = 0;
		}
		else {
			// select the next weapon index
			++WeaponIndex;
		}

		// set the new weapon as current
		CurrentWeapon = OwnedWeapons[WeaponIndex];

		// activate the new weapon
		CurrentWeapon->ActivateWeapon(PlayerTag);
	}
}

void AShooterCharacter::ServerStartFiring_Implementation()
{
	DoStartFiring();
}

void AShooterCharacter::ServerStopFiring_Implementation()
{
	DoStopFiring();
}

void AShooterCharacter::ServerSwitchWeapon_Implementation()
{
	DoSwitchWeapon();
}

void AShooterCharacter::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	if (HasAuthority())
	{
		Weapon->AttachToActor(this, AttachmentRule);
	}

	// attach the weapon meshes
	Weapon->GetFirstPersonMesh()->AttachToComponent(GetFirstPersonMesh(), AttachmentRule, FirstPersonWeaponSocket);
	Weapon->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, ThirdPersonWeaponSocket);
	
}

void AShooterCharacter::PlayFiringMontage(UAnimMontage* Montage)
{
	// stub
}

void AShooterCharacter::AddWeaponRecoil(float Recoil)
{
	// apply the recoil as pitch input
	if (IsLocallyControlled())
	{
		AddControllerPitchInput(Recoil);
	}
}

void AShooterCharacter::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	// Legacy interface hook: ammunition is no longer displayed or consumed.
}

FVector AShooterCharacter::GetWeaponTargetLocation()
{
	// trace ahead from the camera viewpoint
	FHitResult OutHit;

	// Server aiming must not depend on a locally rendered first person camera/mesh.
	const FVector Start = GetPawnViewLocation();
	const FVector End = Start + (GetControlRotation().Vector() * MaxAimDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		QueryParams.AddIgnoredActor(Weapon);
	}

	GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, QueryParams);

	// return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterCharacter::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	if (!HasAuthority() || !WeaponClass || IsDead())
	{
		return;
	}
	// do we already own this weapon?
	AShooterWeapon* OwnedWeapon = FindWeaponOfType(WeaponClass);

	if (!OwnedWeapon)
	{
		// spawn the new weapon
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

		AShooterWeapon* AddedWeapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);

		if (AddedWeapon)
		{
			// add the weapon to the owned list
			OwnedWeapons.Add(AddedWeapon);

			// if we have an existing weapon, deactivate it
			if (CurrentWeapon)
			{
				CurrentWeapon->DeactivateWeapon();
			}

			// switch to the new weapon
			CurrentWeapon = AddedWeapon;
			CurrentWeapon->ActivateWeapon(PlayerTag);
		}
	}
}

void AShooterCharacter::OnWeaponActivated(AShooterWeapon* Weapon)
{
	// set the character mesh AnimInstances
	GetFirstPersonMesh()->SetAnimInstanceClass(Weapon->GetFirstPersonAnimInstanceClass());
	GetMesh()->SetAnimInstanceClass(Weapon->GetThirdPersonAnimInstanceClass());
}

void AShooterCharacter::OnWeaponDeactivated(AShooterWeapon* Weapon)
{
	// unused
}

void AShooterCharacter::OnSemiWeaponRefire()
{
	// unused
}

AShooterWeapon* AShooterCharacter::FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	// check each owned weapon
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (Weapon->IsA(WeaponClass))
		{
			return Weapon;
		}
	}

	// weapon not found
	return nullptr;

}

void AShooterCharacter::Die()
{
	if (!HasAuthority() || bDead)
	{
		return;
	}
	bDead = true;
	// deactivate the weapon
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}

	// increment the team score
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	ApplyDeathPresentation();
	ForceNetUpdate();
	GetWorld()->GetTimerManager().SetTimer(RespawnTimer, this, &AShooterCharacter::OnRespawn, FMath::Max(0.01f, RespawnTime), false);
}

void AShooterCharacter::ApplyDeathPresentation()
{
	if (bDeathPresented)
	{
		return;
	}
	bDeathPresented = true;

	// grant the death tag to the character
	Tags.Add(DeathTag);
		
	// stop character movement
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();

	// disable collision
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// disable controls
	DisableInput(nullptr);

	// reset the bullet counter UI

	// call the BP handler
	BP_OnDeath();

}

void AShooterCharacter::OnRespawn()
{
	// destroy the character to force the PC to respawn
	if (HasAuthority())
	{
		Destroy();
	}
}

bool AShooterCharacter::IsDead() const
{
	// the character is dead if their current HP drops to zero
	return bDead || CurrentHP <= 0.0f;
}

void AShooterCharacter::SetTeam(uint8 Team)
{
	TeamByte = Team;
}
