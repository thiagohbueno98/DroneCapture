#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroneCaptureTarget.generated.h"

class UStaticMeshComponent;
class UStaticMesh;

// Modelos bundlados dentro do proprio Content do plugin (ver
// migrar_modelos_drone_pro_plugin.py) -- cada um com corpo+helice+
// materiais/texturas proprios. "Custom" desliga a troca automatica e usa
// o que estiver manualmente atribuido em BodyMesh/PropellerMesh.
UENUM(BlueprintType)
enum class EDroneModel : uint8
{
	Custom,
	DJIProMini,
	DJI350RTK,
	DJINeo,
	DJIPhantom,
	DJITello,
	// Pawn generico/padrao do AirSim (BP_FlyingPawn sem sufixo) -- malha
	// propria (Content/Models/QuadRotor1 no projeto original).
	AirSimDefault,
};

// Eixo local que ADroneCaptureController::SpinPropellers() incrementa pra
// girar a helice. Mora aqui (por modelo, nao mais fixo no controller) pra
// deixar facil ajustar por modelo se algum precisar de eixo diferente --
// na pratica, todos os 6 modelos validados ao vivo em 2026-09-16 usam Yaw
// (default da struct). Se adicionar um modelo novo e a helice girar
// "errado", teste ao vivo trocando esse campo no Details panel durante o
// Play (lido a cada frame, sem precisar recompilar) antes de mudar aqui.
UENUM(BlueprintType)
enum class EPropellerSpinAxis : uint8
{
	Pitch,
	Yaw,
	Roll,
};

/**
 * Ator do drone (corpo + 4 helices) -- traducao nativa de
 * setup_cena.py::create_or_get_drone_actor(). Vem com 5 modelos de drone
 * bundlados no proprio plugin (dropdown DroneModel); "Custom" libera
 * BodyMesh/PropellerMesh pra um modelo fora da lista.
 */
UCLASS(ClassGroup = (DroneCapture))
class DRONECAPTURE_API ADroneCaptureTarget : public AActor
{
	GENERATED_BODY()

public:
	ADroneCaptureTarget();

	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	EDroneModel DroneModel = EDroneModel::DJIProMini;

	// So tem efeito com DroneModel = Custom -- caso contrario, sao
	// sobrescritos automaticamente pela malha do modelo escolhido acima
	// toda vez que o construction script roda.
	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	UStaticMesh* BodyMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	UStaticMesh* PropellerMesh = nullptr;

	// Relativos ao Corpo. Com DroneModel != Custom, sobrescrito
	// automaticamente pela tabela por-modelo (ver GetModelDefinition() em
	// DroneCaptureTarget.cpp) -- cada modelo tem geometria de braco
	// diferente, entao os 4 transforms nao sao intercambiaveis entre
	// modelos. Ajustavel na mao so com DroneModel = Custom.
	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	TArray<FTransform> PropellerTransforms;

	// Idem PropellerTransforms -- sobrescrito por modelo, ajustavel na mao
	// so com DroneModel = Custom. Lido por ADroneCaptureController::SpinPropellers().
	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	EPropellerSpinAxis PropellerSpinAxis = EPropellerSpinAxis::Roll;

	UPROPERTY(VisibleAnywhere, Category = "Drone Capture")
	UStaticMeshComponent* Corpo;

	UPROPERTY(VisibleAnywhere, Category = "Drone Capture")
	TArray<UStaticMeshComponent*> Propellers;

	virtual void OnConstruction(const FTransform& Transform) override;

	// Troca o modelo em runtime (chamado pelo menu de setup, ver
	// DroneCaptureSetupWidget) -- seta DroneModel e ja reaplica a malha
	// nova, sem precisar de OnConstruction (que so roda no Editor).
	UFUNCTION(BlueprintCallable, Category = "Drone Capture")
	void SetDroneModel(EDroneModel NewModel);

private:
	void ApplyMeshes();
	bool ResolveDroneModelData(EDroneModel Model, UStaticMesh*& OutBody, UStaticMesh*& OutPropeller, TArray<FTransform>& OutPropellerTransforms, EPropellerSpinAxis& OutSpinAxis) const;
};
