// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterBulletCounterUI.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/PanelWidget.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

void UShooterBulletCounterUI::NativeConstruct()
{
	Super::NativeConstruct();
	// The existing HUD also contains LifeBar and DamageOverlay; keep those visible.
	if (!WidgetTree)
	{
		return;
	}
	TArray<UWidget*> Widgets;
	WidgetTree->GetAllWidgets(Widgets);
	for (UWidget* Widget : Widgets)
	{
		if (UImage* Image = Cast<UImage>(Widget))
		{
			const UMaterialInterface* Material = Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject());
			if (Material && Material->GetMaterial() && Material->GetMaterial()->GetFName() == TEXT("M_BulletCounter"))
			{
				Image->SetVisibility(ESlateVisibility::Collapsed);
			}
		}
	}
	ResetHealthPresentation(TargetHealthPercent);
}

void UShooterBulletCounterUI::UpdateHealth(float LifePercent)
{
	TargetHealthPercent = FMath::Clamp(LifePercent, 0.0f, 1.0f);
	if (UProgressBar* LifeBar = Cast<UProgressBar>(GetWidgetFromName(TEXT("LifeBar"))))
	{
		LifeBar->SetPercent(TargetHealthPercent);
	}
}

void UShooterBulletCounterUI::PlayDamageEffect()
{
	HitPulse = 1.0f;
}

void UShooterBulletCounterUI::ResetHealthPresentation(float LifePercent)
{
	StopAllAnimations();
	UpdateHealth(LifePercent);
	DisplayedHealthPercent = TargetHealthPercent;
	HitPulse = 0.0f;
	ApplyHealthPresentation();
}

void UShooterBulletCounterUI::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// Smooth only the presentation; the health bar always uses authoritative HP.
	const float Blend = 1.0f - FMath::Exp(-3.0f * FMath::Max(0.0f, InDeltaTime));
	DisplayedHealthPercent = FMath::Lerp(DisplayedHealthPercent, TargetHealthPercent, Blend);
	HitPulse = FMath::Max(0.0f, HitPulse - InDeltaTime / 0.45f);
	ApplyHealthPresentation();
}

void UShooterBulletCounterUI::ApplyHealthPresentation()
{
	const float MissingHealth = 1.0f - DisplayedHealthPercent;
	const float PersistentRed = 0.65f * MissingHealth * MissingHealth;
	// Healthy hits are pale red; at low health both the flash and border deepen.
	const FLinearColor Tint(1.0f, 0.65f * DisplayedHealthPercent,
		0.65f * DisplayedHealthPercent, 1.0f);
	if (UWidget* Overlay = GetWidgetFromName(TEXT("DamageOverlay")))
	{
		Overlay->SetVisibility(ESlateVisibility::HitTestInvisible);
		Overlay->SetRenderOpacity(FMath::Clamp(PersistentRed + 0.35f * HitPulse, 0.0f, 0.85f));
		TArray<UWidget*> PendingWidgets { Overlay };
		while (!PendingWidgets.IsEmpty())
		{
			UWidget* Widget = PendingWidgets.Pop();
			if (UBorder* Border = Cast<UBorder>(Widget))
			{
				Border->SetBrushColor(Tint);
			}
			else if (UImage* Image = Cast<UImage>(Widget))
			{
				Image->SetColorAndOpacity(Tint);
			}
			if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
			{
				PendingWidgets.Append(Panel->GetAllChildren());
			}
		}
	}
}

