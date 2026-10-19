#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "ShooterCharacter.h"
#include "ShooterProjectile.h"
#include "Components/CapsuleComponent.h"
#include "ShooterWeapon.h"
#include "ShooterBulletCounterUI.h"
#include "EngineUtils.h"
#include "Components/Widget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Engine.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterServerDamageTest,
	"MyProject.Multiplayer.ServerDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterServerDamageTest::RunTest(const FString& Parameters)
{
	UClass* CharacterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* ProjectileClass = LoadClass<AShooterProjectile>(nullptr,
		TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Projectiles/BP_ShooterProjectile_Bullet.BP_ShooterProjectile_Bullet_C"));
	if (!TestNotNull(TEXT("Existing shooter Blueprint loads"), CharacterClass) ||
		!TestNotNull(TEXT("Existing bullet Blueprint loads"), ProjectileClass))
	{
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Attacker = World->SpawnActor<AShooterCharacter>(CharacterClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	AShooterCharacter* Victim = World->SpawnActor<AShooterCharacter>(CharacterClass, FVector(500, 0, 0), FRotator::ZeroRotator, SpawnParams);
	SpawnParams.Owner = Attacker;
	SpawnParams.Instigator = Attacker;
	AActor* Bullet = World->SpawnActor<AShooterProjectile>(ProjectileClass, FVector(400, 0, 0), FRotator::ZeroRotator, SpawnParams);
	if (!Attacker || !Victim || !Bullet)
	{
		AddError(TEXT("Could not spawn the shooter test actors"));
		World->DestroyWorld(false);
		return false;
	}

	const float InitialHealth = Victim->GetHealthPercent();
	FDamageEvent DamageEvent;
	Victim->SetRole(ROLE_AutonomousProxy);
	TestEqual(TEXT("Client-side damage is rejected"), Victim->TakeDamage(100.0f, DamageEvent, nullptr, Attacker), 0.0f);
	TestEqual(TEXT("Client cannot modify health"), Victim->GetHealthPercent(), InitialHealth);
	Victim->SetRole(ROLE_Authority);
	TestEqual(TEXT("Negative damage is rejected"), Victim->TakeDamage(-100.0f, DamageEvent, nullptr, Attacker), 0.0f);

	FHitResult Hit(Victim, Victim->GetCapsuleComponent(), Victim->GetActorLocation(), -FVector::ForwardVector);
	Bullet->SetRole(ROLE_SimulatedProxy);
	Bullet->NotifyHit(nullptr, Victim, Victim->GetCapsuleComponent(), true,
		Hit.ImpactPoint, Hit.ImpactNormal, FVector::ZeroVector, Hit);
	TestEqual(TEXT("Client projectile impact cannot damage another player"), Victim->GetHealthPercent(), InitialHealth);

	Bullet->SetRole(ROLE_Authority);
	Bullet->NotifyHit(nullptr, Victim, Victim->GetCapsuleComponent(), true,
		Hit.ImpactPoint, Hit.ImpactNormal, FVector::ZeroVector, Hit);
	const float DamagedHealth = Victim->GetHealthPercent();
	TestTrue(TEXT("Server projectile impact damages another player"), DamagedHealth < InitialHealth);
	TestEqual(TEXT("Attacker is not damaged"), Attacker->GetHealthPercent(), InitialHealth);
	if (IsValid(Bullet))
	{
		Bullet->NotifyHit(nullptr, Victim, Victim->GetCapsuleComponent(), true,
			Hit.ImpactPoint, Hit.ImpactNormal, FVector::ZeroVector, Hit);
		TestEqual(TEXT("One projectile cannot apply its impact twice"), Victim->GetHealthPercent(), DamagedHealth);
	}

	Victim->TakeDamage(100000.0f, DamageEvent, nullptr, Attacker);
	TestTrue(TEXT("Server lethal damage marks the victim dead"), Victim->IsDead());
	TestEqual(TEXT("Lethal damage clamps health to zero"), Victim->GetHealthPercent(), 0.0f);
	TestEqual(TEXT("Dead players reject additional damage"), Victim->TakeDamage(100.0f, DamageEvent, nullptr, Attacker), 0.0f);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShooterHealthFiringTest,
	"MyProject.Multiplayer.HealthPoweredFiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FShooterHealthFiringTest::RunTest(const FString& Parameters)
{
	UClass* CharacterClass = LoadClass<AShooterCharacter>(nullptr,
		TEXT("/Game/Variant_Shooter/Blueprints/BP_ShooterCharacter.BP_ShooterCharacter_C"));
	UClass* WeaponClass = LoadClass<AShooterWeapon>(nullptr,
		TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C"));
	UClass* HUDClass = LoadClass<UShooterBulletCounterUI>(nullptr,
		TEXT("/Game/Variant_Shooter/UI/UI_ShooterBulletCounter.UI_ShooterBulletCounter_C"));
	if (!CharacterClass || !WeaponClass || !HUDClass)
	{
		AddError(TEXT("Health-powered firing test assets could not be loaded"));
		return false;
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	};
	World->Tick(LEVELTICK_All, 1.0f);
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AShooterCharacter* Shooter = World->SpawnActor<AShooterCharacter>(CharacterClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	AShooterCharacter* Victim = World->SpawnActor<AShooterCharacter>(CharacterClass, FVector(500, 0, 0), FRotator::ZeroRotator, SpawnParams);
	SpawnParams.Owner = Shooter;
	SpawnParams.Instigator = Shooter;
	AShooterWeapon* Weapon = World->SpawnActor<AShooterWeapon>(WeaponClass, FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!Shooter || !Victim || !Weapon)
	{
		AddError(TEXT("Could not spawn the health-powered firing test actors"));
		return false;
	}
	// This isolated world has no local player/camera initialization. Animation is
	// outside this gameplay test; keep its Blueprint classes off this weapon instance.
	for (FName PropertyName : { FName(TEXT("FirstPersonAnimInstanceClass")), FName(TEXT("ThirdPersonAnimInstanceClass")) })
	{
		if (FClassProperty* Property = FindFProperty<FClassProperty>(Weapon->GetClass(), PropertyName))
		{
			Property->SetObjectPropertyValue_InContainer(Weapon, nullptr);
		}
	}
	Weapon->InitializeOwner();
	FDamageEvent DamageEvent;
	Shooter->TakeDamage(100.0f, DamageEvent, nullptr, Victim);
	Shooter->SetRole(ROLE_AutonomousProxy);
	TestFalse(TEXT("Client cannot spend health directly"), Shooter->TrySpendHealthForShot());
	TestEqual(TEXT("Client cannot heal directly"), Shooter->RestoreHealth(7.0f), 0.0f);
	Shooter->SetRole(ROLE_Authority);

	Weapon->StartFiring();
	Weapon->StopFiring();
	TestEqual(TEXT("Actual weapon firing spends five health"), Shooter->GetHealthPercent(), 395.0f / 500.0f);
	AShooterProjectile* Shot = nullptr;
	for (TActorIterator<AShooterProjectile> It(World); It; ++It)
	{
		if (It->GetInstigator() == Shooter)
		{
			Shot = *It;
			break;
		}
	}
	if (TestNotNull(TEXT("Health-powered weapon produces a projectile"), Shot))
	{
		AActor* ShotActor = Shot;
		FHitResult Hit(Victim, Victim->GetCapsuleComponent(), Victim->GetActorLocation(), -FVector::ForwardVector);
		ShotActor->NotifyHit(nullptr, Victim, Victim->GetCapsuleComponent(), true,
			Hit.ImpactPoint, Hit.ImpactNormal, FVector::ZeroVector, Hit);
		TestEqual(TEXT("Actual damaging impact restores seven health, net gain two"), Shooter->GetHealthPercent(), 402.0f / 500.0f);
		if (IsValid(ShotActor))
		{
			ShotActor->NotifyHit(nullptr, Victim, Victim->GetCapsuleComponent(), true,
				Hit.ImpactPoint, Hit.ImpactNormal, FVector::ZeroVector, Hit);
			TestEqual(TEXT("Duplicate impact cannot heal twice"), Shooter->GetHealthPercent(), 402.0f / 500.0f);
		}
	}
	Shooter->RestoreHealth(100000.0f);
	TestEqual(TEXT("Healing is capped at maximum health"), Shooter->GetHealthPercent(), 1.0f);
	Shooter->TakeDamage(495.0f, DamageEvent, nullptr, Victim);
	TestFalse(TEXT("Last five health cannot be spent on a shot"), Shooter->TrySpendHealthForShot());
	TestFalse(TEXT("Insufficient health does not kill the shooter"), Shooter->IsDead());
	Shooter->TakeDamage(100.0f, DamageEvent, nullptr, Victim);
	TestEqual(TEXT("Delayed hits cannot revive a dead shooter"), Shooter->RestoreHealth(7.0f), 0.0f);

	UShooterBulletCounterUI* HUD = NewObject<UShooterBulletCounterUI>(World, HUDClass);
	HUD->Initialize();
	HUD->TakeWidget();
	UWidget* AmmoDisplay = nullptr;
	TArray<UWidget*> Widgets;
	HUD->WidgetTree->GetAllWidgets(Widgets);
	for (UWidget* Widget : Widgets)
	{
		if (const UImage* Image = Cast<UImage>(Widget))
		{
			const UMaterialInterface* Material = Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject());
			if (Material && Material->GetMaterial() && Material->GetMaterial()->GetFName() == TEXT("M_BulletCounter"))
			{
				AmmoDisplay = Widget;
			}
		}
	}
	UWidget* LifeBar = HUD->GetWidgetFromName(TEXT("LifeBar"));
	if (TestNotNull(TEXT("Existing ammo display is found"), AmmoDisplay))
	{
		TestTrue(TEXT("Ammo display is collapsed"), AmmoDisplay->GetVisibility() == ESlateVisibility::Collapsed);
	}
	if (TestNotNull(TEXT("Existing health bar is retained"), LifeBar))
	{
		TestTrue(TEXT("Health bar remains visible"), LifeBar->GetVisibility() != ESlateVisibility::Collapsed);
	}
	return true;
}

#endif
