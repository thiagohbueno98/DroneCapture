#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroneCaptureGridVolume.generated.h"

class UBoxComponent;

/**
 * Volume que marca a regiao 3D onde o grid de captura varre. Um
 * UBoxComponent puro nunca renderiza nada (so um wireframe no Editor
 * quando selecionado) -- diferente do Cube manual usado antes, nao
 * precisa de nenhum truque de "esconder da captura".
 */
UCLASS(ClassGroup = (DroneCapture))
class DRONECAPTURE_API ADroneCaptureGridVolume : public AActor
{
	GENERATED_BODY()

public:
	ADroneCaptureGridVolume();

	UPROPERTY(VisibleAnywhere, Category = "Drone Capture")
	UBoxComponent* Bounds;
};
