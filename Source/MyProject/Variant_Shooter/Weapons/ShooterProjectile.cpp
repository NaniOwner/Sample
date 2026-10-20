// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterProjectile.h"
#include "Components/SphereComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "ShooterCharacter.h"

AShooterProjectile::AShooterProjectile()
{
	bReplicates = true;
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick = true;

	// create the collision component and assign it as the root
	RootComponent = CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("Collision Component"));

	CollisionComponent->SetSphereRadius(16.0f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionComponent->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;

	// create the projectile movement component. No need to attach it because it's not a Scene Component
	ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("Projectile Movement"));

	ProjectileMovement->InitialSpeed = 3000.0f;
	ProjectileMovement->MaxSpeed = 3000.0f;
	ProjectileMovement->bShouldBounce = true;

	// set the default damage type
	HitDamageType = UDamageType::StaticClass();
}

bool AShooterProjectile::IsShooterActor(const AActor* Actor) const
{
	return Actor && (Actor == GetInstigator() || Actor == GetOwner() ||
		(GetInstigator() && Actor->IsOwnedBy(GetInstigator())));
}

void AShooterProjectile::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Apply after Blueprint defaults so every projectile ignores other projectiles.
	TInlineComponentArray<UPrimitiveComponent*> ProjectileComponents(this);
	for (UPrimitiveComponent* Component : ProjectileComponents)
	{
		Component->SetCollisionObjectType(ECC_GameTraceChannel1);
		Component->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Ignore);
	}
	InitializeShooterCollision();
}

void AShooterProjectile::InitializeShooterCollision()
{
	TArray<AActor*> IgnoredActors;
	if (GetInstigator())
	{
		IgnoredActors.AddUnique(GetInstigator());
		GetInstigator()->GetAttachedActors(IgnoredActors, false, true);
	}
	if (GetOwner())
	{
		IgnoredActors.AddUnique(GetOwner());
	}
	for (AActor* Actor : IgnoredActors)
	{
		CollisionComponent->IgnoreActorWhenMoving(Actor, true);
		TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
		for (UPrimitiveComponent* Component : Components)
		{
			Component->IgnoreActorWhenMoving(this, true);
			ShooterCollisionComponents.AddUnique(Component);
		}
	}
}

void AShooterProjectile::BeginPlay()
{
	InitializeShooterCollision();
	Super::BeginPlay();
	
	// ignore the pawn that shot this projectile
	CollisionComponent->IgnoreActorWhenMoving(GetInstigator(), true);
	if (!HasAuthority())
	{
		// Only the server simulates collisions and movement; clients display replication.
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ProjectileMovement->Deactivate();
	}
}

void AShooterProjectile::ClearShooterCollision()
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& Component : ShooterCollisionComponents)
	{
		if (Component.IsValid())
		{
			Component->IgnoreActorWhenMoving(this, false);
		}
	}
	ShooterCollisionComponents.Empty();
}

void AShooterProjectile::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	ClearShooterCollision();
	Super::EndPlay(EndPlayReason);

	// clear the destruction timer
	GetWorld()->GetTimerManager().ClearTimer(DestructionTimer);
}

void AShooterProjectile::NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, class UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	// ignore if we've already hit something else
	if (!HasAuthority() || bHit || IsShooterActor(Other) || Cast<AShooterProjectile>(Other))
	{
		return;
	}

	bHit = true;

	// disable collision on the projectile
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ClearShooterCollision();

	// make AI perception noise
	MakeNoise(NoiseLoudness, GetInstigator(), GetActorLocation(), NoiseRange, NoiseTag);

	if (bExplodeOnHit)
	{
		
		// apply explosion damage centered on the projectile
		ExplosionCheck(GetActorLocation());

	} else {

		// single hit projectile. Process the collided actor
		ProcessHit(Other, OtherComp, Hit.ImpactPoint, -Hit.ImpactNormal);

	}

	// pass control to BP for any extra effects
	MulticastProjectileHit(Hit);

	// check if we should schedule deferred destruction of the projectile
	if (DeferredDestructionTime > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(DestructionTimer, this, &AShooterProjectile::OnDeferredDestruction, DeferredDestructionTime, false);

	} else {

		// destroy the projectile right away
		Destroy();
	}
}

void AShooterProjectile::ExplosionCheck(const FVector& ExplosionCenter)
{
	if (!HasAuthority())
	{
		return;
	}
	// do a sphere overlap check look for nearby actors to damage
	TArray<FOverlapResult> Overlaps;

	FCollisionShape OverlapShape;
	OverlapShape.SetSphere(ExplosionRadius);

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	if (!bDamageOwner)
	{
		QueryParams.AddIgnoredActor(GetInstigator());
	}

	GetWorld()->OverlapMultiByObjectType(Overlaps, ExplosionCenter, FQuat::Identity, ObjectParams, OverlapShape, QueryParams);

	TArray<AActor*> DamagedActors;

	// process the overlap results
	for (const FOverlapResult& CurrentOverlap : Overlaps)
	{
		// overlaps may return the same actor multiple times per each component overlapped
		// ensure we only damage each actor once by adding it to a damaged list
		if (DamagedActors.Find(CurrentOverlap.GetActor()) == INDEX_NONE)
		{
			DamagedActors.Add(CurrentOverlap.GetActor());

			// apply physics force away from the explosion
			const FVector& ExplosionDir = CurrentOverlap.GetActor()->GetActorLocation() - GetActorLocation();

			// push and/or damage the overlapped actor
			ProcessHit(CurrentOverlap.GetActor(), CurrentOverlap.GetComponent(), GetActorLocation(), ExplosionDir.GetSafeNormal());
		}
			
	}
}

void AShooterProjectile::ProcessHit(AActor* HitActor, UPrimitiveComponent* HitComp, const FVector& HitLocation, const FVector& HitDirection)
{
	if (!HasAuthority() || !IsValid(HitActor))
	{
		return;
	}
	// have we hit a character?
	if (ACharacter* HitCharacter = Cast<ACharacter>(HitActor))
	{
		// ignore the owner of this projectile
		if (!IsShooterActor(HitCharacter) || bDamageOwner)
		{
			// apply damage to the character
			const float AppliedDamage = UGameplayStatics::ApplyDamage(HitCharacter, HitDamage, GetInstigatorController(), this, HitDamageType);
			if (AppliedDamage > 0.0f && HitCharacter != GetInstigator() && !bGrantedHitHealing)
			{
				// A grenade hitting several targets still grants only one healing reward.
				bGrantedHitHealing = true;
				if (AShooterCharacter* Shooter = Cast<AShooterCharacter>(GetInstigator()))
				{
					Shooter->RestoreHealth(HitHealthRestore);
				}
			}
		}
	}

	// have we hit a physics object?
	if (IsValid(HitComp) && HitComp->IsSimulatingPhysics())
	{
		// give some physics impulse to the object
		HitComp->AddImpulseAtLocation(HitDirection * PhysicsForce, HitLocation);
	}
}

void AShooterProjectile::MulticastProjectileHit_Implementation(const FHitResult& Hit)
{
	ProjectileMovement->StopMovementImmediately();
	ProjectileMovement->Deactivate();
	if (GetNetMode() != NM_DedicatedServer)
	{
		BP_OnProjectileHit(Hit);
	}
}

void AShooterProjectile::OnDeferredDestruction()
{
	// destroy this actor
	Destroy();
}

void AShooterProjectile::SetNoiseTag(const FName& Tag)
{
	NoiseTag = Tag;
}

void AShooterProjectile::SetHitHealthRestore(float Amount)
{
	if (HasAuthority())
	{
		HitHealthRestore = FMath::IsFinite(Amount) ? FMath::Max(0.0f, Amount) : 0.0f;
	}
}
