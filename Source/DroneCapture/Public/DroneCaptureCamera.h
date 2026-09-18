#pragma once

#include "CoreMinimal.h"
#include "Engine/SceneCapture2D.h"
#include "DroneCaptureCamera.generated.h"

class USceneCaptureComponent2D;

/**
 * Camera de captura pronta pra uso -- traducao nativa de
 * setup_cena.py::configure_camera()/create_or_get_render_target(). Cria o
 * proprio Render Target em runtime (sem salvar asset nenhum no Content
 * Browser) e aplica todos os overrides de pos-processo/Lumen/exposicao
 * validados no fluxo Python. So arrastar no nivel, posicionar, e setar
 * CameraIndex (1,2,3...) -- sem precisar de Tag manual.
 */
UCLASS(ClassGroup = (DroneCapture))
class DRONECAPTURE_API ADroneCaptureCamera : public ASceneCapture2D
{
	GENERATED_BODY()

public:
	ADroneCaptureCamera();

	// Ordem entre as cameras -- usado por ADroneCaptureController pra
	// descobrir/ordenar automaticamente (GetAllActorsOfClass), sem
	// depender de Actor Tag manual.
	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	int32 CameraIndex = 0;

	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	int32 RtWidth = 1920;

	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	int32 RtHeight = 1080;

	// Fator de supersampling -- a camera renderiza numa resolucao
	// SupersampleFactor vezes maior (RtWidth*Factor x RtHeight*Factor) e o
	// controller reduz pra RtWidth x RtHeight (media dos texels num "box
	// filter", em espaco linear) antes de salvar o PNG final. RtWidth/
	// RtHeight continuam sendo a resolucao FINAL do dataset -- so a
	// renderizacao interna fica maior. 1 = desliga (comportamento antigo,
	// exporta direto na resolucao de RtWidth/RtHeight, sem custo extra).
	// Mesma ideia de Super-Sampling Anti-Aliasing (SSAA): reduz
	// ruido/serrilhado e deixa a imagem mais "natural".
	UPROPERTY(EditAnywhere, Category = "Drone Capture", meta = (ClampMin = "1", ClampMax = "4"))
	int32 SupersampleFactor = 2;

	// Segunda captura, so pra mascara de segmentacao do drone (ver
	// ADroneCaptureController::CheckPoseFromMask) -- acha oclusao/bbox a
	// partir dos pixels de verdade em vez de raycast+projecao geometrica.
	// Sempre na resolucao FINAL (RtWidth x RtHeight, sem supersample --
	// mascara binaria nao precisa de anti-aliasing extra).
	USceneCaptureComponent2D* GetMaskCaptureComponent() const { return MaskCaptureComponent; }

protected:
	virtual void BeginPlay() override;

private:
	void CreateRenderTarget();
	void ConfigurePostProcess();
	void CopyLevelLookIntoCapture();

	UPROPERTY()
	USceneCaptureComponent2D* MaskCaptureComponent = nullptr;
};
