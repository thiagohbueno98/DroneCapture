#include "DroneCaptureGridVolume.h"

#include "Components/BoxComponent.h"

ADroneCaptureGridVolume::ADroneCaptureGridVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	Bounds->SetBoxExtent(FVector(500.0f, 500.0f, 300.0f));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetGenerateOverlapEvents(false);
	RootComponent = Bounds;

	Tags.Add(TEXT("VolumeGrid"));
}
