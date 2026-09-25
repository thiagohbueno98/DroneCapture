#include "DroneCaptureGridVolume.h"

#include "Components/BoxComponent.h"
#include "Components/WorldPartitionStreamingSourceComponent.h"

ADroneCaptureGridVolume::ADroneCaptureGridVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	Bounds->SetBoxExtent(FVector(500.0f, 500.0f, 300.0f));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetGenerateOverlapEvents(false);
	RootComponent = Bounds;

	StreamingSource = CreateDefaultSubobject<UWorldPartitionStreamingSourceComponent>(TEXT("StreamingSource"));
	StreamingSource->Priority = EStreamingSourcePriority::High;

#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif

	Tags.Add(TEXT("VolumeGrid"));
}
