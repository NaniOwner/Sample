// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterBulletCounterUI.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
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
}

