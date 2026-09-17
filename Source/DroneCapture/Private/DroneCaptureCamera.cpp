#include "DroneCaptureCamera.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/PostProcessVolume.h"
#include "EngineUtils.h"

ADroneCaptureCamera::ADroneCaptureCamera()
{
	if (USceneCaptureComponent2D* Comp = GetCaptureComponent2D())
	{
		Comp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		Comp->bCaptureEveryFrame = false;
		Comp->bCaptureOnMovement = false;
	}
}

void ADroneCaptureCamera::BeginPlay()
{
	Super::BeginPlay();

	CreateRenderTarget();
	ConfigurePostProcess();
	CopyLevelLookIntoCapture();

	Tags.AddUnique(TEXT("CaptureCam"));
	if (CameraIndex > 0)
	{
		Tags.AddUnique(FName(*FString::Printf(TEXT("CaptureCam%d"), CameraIndex)));
	}
}

void ADroneCaptureCamera::CreateRenderTarget()
{
	USceneCaptureComponent2D* Comp = GetCaptureComponent2D();
	if (!Comp)
	{
		return;
	}

	const int32 Factor = FMath::Max(1, SupersampleFactor);

	UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(this);
	RT->RenderTargetFormat = RTF_RGBA8;
	RT->ClearColor = FLinearColor::Black;
	RT->bAutoGenerateMips = false;
	RT->InitAutoFormat(RtWidth * Factor, RtHeight * Factor);
	RT->UpdateResourceImmediate(true);

	Comp->TextureTarget = RT;
	Comp->bIgnoreScreenPercentage = true;
	Comp->bAlwaysPersistRenderingState = true;
}

void ADroneCaptureCamera::ConfigurePostProcess()
{
	USceneCaptureComponent2D* Comp = GetCaptureComponent2D();
	if (!Comp)
	{
		return;
	}

	FPostProcessSettings& Pps = Comp->PostProcessSettings;

	// SceneCaptureComponent2D desliga Lumen (GI + reflexos) por padrao --
	// sem isso a imagem sai bem mais "chapada"/lavada que o viewport.
	Pps.bOverride_DynamicGlobalIlluminationMethod = true;
	Pps.DynamicGlobalIlluminationMethod = EDynamicGlobalIlluminationMethod::Lumen;
	Pps.bOverride_ReflectionMethod = true;
	Pps.ReflectionMethod = EReflectionMethod::Lumen;

	// Auto exposure precisa de tempo real passando entre frames pra
	// convergir -- forcar velocidade maxima faz isso quase instantaneo.
	Pps.bOverride_AutoExposureSpeedUp = true;
	Pps.AutoExposureSpeedUp = 20.0f;
	Pps.bOverride_AutoExposureSpeedDown = true;
	Pps.AutoExposureSpeedDown = 20.0f;

	// Efeitos pensados pra camera de jogo/cinematica, nao pra "vigilancia"
	// estatica -- so introduzem blur/ruido que o detector nao deveria
	// aprender.
	Pps.bOverride_MotionBlurAmount = true;
	Pps.MotionBlurAmount = 0.0f;
	Pps.bOverride_SceneFringeIntensity = true;
	Pps.SceneFringeIntensity = 0.0f;
	Pps.bOverride_VignetteIntensity = true;
	Pps.VignetteIntensity = 0.0f;
	Pps.bOverride_FilmGrainIntensity = true;
	Pps.FilmGrainIntensity = 0.0f;
	Pps.bOverride_LensFlareIntensity = true;
	Pps.LensFlareIntensity = 0.0f;
	Pps.bOverride_DepthOfFieldFstop = true;
	Pps.DepthOfFieldFstop = 32.0f; // DOF praticamente infinito

	Comp->PostProcessBlendWeight = 1.0f;
}

void ADroneCaptureCamera::CopyLevelLookIntoCapture()
{
	// (ver setup_cena.py::copy_level_look_into_capture) SceneCaptureComponent2D
	// nao herda o blend de PostProcessVolume do nivel do jeito que uma
	// camera pilotada normal herda -- copia manual dos campos de "look"
	// (nao inclui vignette de proposito, ja zerada acima).
	APostProcessVolume* RefVolume = nullptr;
	for (TActorIterator<APostProcessVolume> It(GetWorld()); It; ++It)
	{
		RefVolume = *It;
		break;
	}
	if (!RefVolume)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneCapture] Nenhum PostProcessVolume encontrado no nivel -- captura pode sair mais lavada/chapada que o viewport."));
		return;
	}

	USceneCaptureComponent2D* Comp = GetCaptureComponent2D();
	const FPostProcessSettings& Ref = RefVolume->Settings;
	FPostProcessSettings& Pps = Comp->PostProcessSettings;

	if (Ref.bOverride_BloomIntensity)
	{
		Pps.bOverride_BloomIntensity = true;
		Pps.BloomIntensity = Ref.BloomIntensity;
	}
	if (Ref.bOverride_ColorSaturation)
	{
		Pps.bOverride_ColorSaturation = true;
		Pps.ColorSaturation = Ref.ColorSaturation;
	}
	if (Ref.bOverride_WhiteTemp)
	{
		Pps.bOverride_WhiteTemp = true;
		Pps.WhiteTemp = Ref.WhiteTemp;
	}
	if (Ref.bOverride_WhiteTint)
	{
		Pps.bOverride_WhiteTint = true;
		Pps.WhiteTint = Ref.WhiteTint;
	}
	if (Ref.bOverride_AmbientOcclusionIntensity)
	{
		Pps.bOverride_AmbientOcclusionIntensity = true;
		Pps.AmbientOcclusionIntensity = Ref.AmbientOcclusionIntensity;
	}
	if (Ref.bOverride_AmbientOcclusionStaticFraction)
	{
		Pps.bOverride_AmbientOcclusionStaticFraction = true;
		Pps.AmbientOcclusionStaticFraction = Ref.AmbientOcclusionStaticFraction;
	}
	if (Ref.bOverride_ScreenSpaceReflectionIntensity)
	{
		Pps.bOverride_ScreenSpaceReflectionIntensity = true;
		Pps.ScreenSpaceReflectionIntensity = Ref.ScreenSpaceReflectionIntensity;
	}
}
