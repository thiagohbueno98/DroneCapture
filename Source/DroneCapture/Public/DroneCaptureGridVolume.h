#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroneCaptureGridVolume.generated.h"

class UBoxComponent;
class UWorldPartitionStreamingSourceComponent;

/**
 * Volume que marca a regiao 3D onde o grid de captura varre. Um
 * UBoxComponent puro nunca renderiza nada (so um wireframe no Editor
 * quando selecionado) -- diferente do Cube manual usado antes, nao
 * precisa de nenhum truque de "esconder da captura".
 *
 * Tambem e fonte de streaming do World Partition: sem isso, so a regiao
 * em volta do jogador (PlayerStart) carrega, e grid/drone/cameras longe
 * dele nem existiam no Play (e o cenario da captura vinha incompleto).
 * O volume e os outros atores do plugin sao "sempre carregados"
 * (bIsSpatiallyLoaded=false) pra poderem ser achados logo no BeginPlay.
 */
UCLASS(ClassGroup = (DroneCapture))
class DRONECAPTURE_API ADroneCaptureGridVolume : public AActor
{
	GENERATED_BODY()

public:
	ADroneCaptureGridVolume();

	UPROPERTY(VisibleAnywhere, Category = "Drone Capture")
	UBoxComponent* Bounds;

	// Carrega o mapa em volta do centro do volume, com o raio de
	// carregamento padrao da grade do World Partition.
	UPROPERTY(VisibleAnywhere, Category = "Drone Capture")
	UWorldPartitionStreamingSourceComponent* StreamingSource;
};
