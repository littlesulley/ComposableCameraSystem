// Copyright Sulley. All rights reserved.

#include "Variables/ComposableCameraVariableCollection.h"
#include "Variables/ComposableCameraVariable.h"
#include "ComposableCameraSystemModule.h"

UComposableCameraVariableCollection::UComposableCameraVariableCollection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UComposableCameraVariableCollection::PostLoad()
{
	UObject::PostLoad();

#if WITH_EDITOR
	for (UComposableCameraVariable* Variable : Variables)
	{
		if (Variable && !Variable->HasAnyFlags(RF_Public))
		{
			UE_LOG(LogComposableCameraSystem, Warning, TEXT("Adding missing RF_Public flag on variable '%s'."), *GetPathNameSafe(Variable));
			Variable->SetFlags(RF_Public);
		}
	}
	
	CleanUpStrayObjects();
#endif
}

#if WITH_EDITOR
void UComposableCameraVariableCollection::CleanUpStrayObjects()
{
	UPackage* CollectionPackage = GetOutermost();
	if (!CollectionPackage || CollectionPackage == GetTransientPackage())
	{
		return;
	}

	TSet<UObject*> StrayObjects;
	TSet<UComposableCameraVariable*> KnownVariables(Variables);

	TArray<UObject*> ObjectsInPackage;
	GetObjectsWithPackage(CollectionPackage, ObjectsInPackage);
	for (UObject* Object : ObjectsInPackage)
	{
		if (UComposableCameraVariable* Variable = Cast<UComposableCameraVariable>(Object))
		{
			if (KnownVariables.Contains(Variable))
			{
				continue;
			}

			Modify();
			Variable->ClearFlags(RF_Public | RF_Standalone);
			StrayObjects.Add(Variable);
		}
	}

	if (StrayObjects.Num() > 0)
	{
		for (UObject* Object : ObjectsInPackage)
		{
			if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Object))
			{
				if (StrayObjects.Contains(Redirector->DestinationObject))
				{
					Redirector->ClearFlags(RF_Public | RF_Standalone);
					Redirector->DestinationObject = nullptr;
				}
			}
		}

		UE_LOG(LogComposableCameraSystem, Warning,
		TEXT("Cleaned up %d stray camera variables in camera variable collection '%s'. Please resave the asset."),
		StrayObjects.Num(), *GetPathNameSafe(this));
	}
}
#endif
