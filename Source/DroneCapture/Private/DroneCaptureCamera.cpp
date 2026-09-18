#include "DroneCaptureCamera.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Scene.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"

namespace
{
	// Vinheta/grao de filme/lens flare/motion blur/bloom sao aplicados numa
	// etapa DEPOIS de BL_SCENE_COLOR_BEFORE_DOF (onde M_DroneMask substitui
	// a cor) -- sem desligar isso na captura de mascara tambem, esses
	// efeitos continuam manchando o preto/branco solido por cima (achado
	// rodando de verdade: vinheta/grao visivel nas bordas mesmo com
	// Opacity=1 no material). DOF tambem borraria a borda nitida da
	// silhueta -- fstop bem alto deixa o DOF praticamente sem efeito.
	void DisableArtisticPostProcessEffects(FPostProcessSettings& Pps)
	{
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
		Pps.DepthOfFieldFstop = 32.0f;
		Pps.bOverride_BloomIntensity = true;
		Pps.BloomIntensity = 0.0f;
	}
}

ADroneCaptureCamera::ADroneCaptureCamera()
{
	if (USceneCaptureComponent2D* Comp = GetCaptureComponent2D())
	{
		Comp->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		Comp->bCaptureEveryFrame = false;
		Comp->bCaptureOnMovement = false;
	}

	// Segunda captura so pra mascara de segmentacao do drone -- material
	// PROPRIO (/DroneCapture/Materials/M_DroneMask, criado via
	// ue_python/criar_material_mascara_drone.py), nao o material de "Buffer
	// Visualization" do Engine (/Engine/BufferVisualization/CustomStencil):
	// testado 2026-09-17, esse saia branco/estourado -- mesmo sintoma do
	// gotcha ja documentado em contexto.md (BL_SCENE_COLOR_AFTER_TONEMAPPING
	// nao acessa CustomStencil direito). M_DroneMask usa
	// BL_SCENE_COLOR_BEFORE_DOF (confirmado funcionando) + comparacao
	// explicita no proprio material (preto/branco solido, sem depender de
	// tonemapping/exposicao pra separar os dois). A cena inteira continua
	// sendo desenhada normalmente nessa passada -- oclusao de verdade
	// funciona igual a RGB, so a cor final e substituida.
	MaskCaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("MaskCaptureComponent"));
	MaskCaptureComponent->SetupAttachment(RootComponent);
	MaskCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	MaskCaptureComponent->bCaptureEveryFrame = false;
	MaskCaptureComponent->bCaptureOnMovement = false;

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaskMaterialFinder(TEXT("/DroneCapture/Materials/M_DroneMask.M_DroneMask"));
	if (MaskMaterialFinder.Succeeded())
	{
		MaskCaptureComponent->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, MaskMaterialFinder.Object));
	}
	MaskCaptureComponent->PostProcessBlendWeight = 1.0f;

	// Desliga TAA/TSR SO nesta captura -- a mascara e 1 frame estatico
	// (nao precisa de acumulo temporal, ao contrario da captura RGB que
	// espera WarmupCaptures frames de Lumen/SSR convergindo). Achado
	// rodando o dataset de verdade: com TAA/TSR ligado, CustomDepth e
	// SceneDepth (usados na comparacao de oclusao do material, ver
	// M_DroneMask) sao amostrados com jitter sub-pixel LEVEMENTE diferente
	// entre os 2 buffers (renderizados em passadas distintas), gerando
	// ruido "pontilhado" na propria silhueta do drone mesmo sem oclusao
	// real -- e um dos fatores que tambem contribuia pro falso-positivo de
	// oclusao perto da agua da fonte (ver contexto.md).
	MaskCaptureComponent->ShowFlags.SetTemporalAA(false);
	MaskCaptureComponent->ShowFlags.SetAntiAliasing(false);

	// NAO forcar AEM_Manual aqui -- achado rodando de verdade (testes via
	// Python Editor Scripting, ver contexto.md): exposicao manual sem
	// TAMBEM configurar abertura/ISO/shutter deixa a cena escura demais e
	// apaga o branco solido do material (emissive 10.0) de volta pra
	// preto. O Opacity=1 do material ja substitui a cor da cena inteira,
	// entao o auto-exposure padrao (Histogram) funciona bem o suficiente
	// sem precisar fixar nada aqui.

	DisableArtisticPostProcessEffects(MaskCaptureComponent->PostProcessSettings);
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

	if (MaskCaptureComponent)
	{
		UTextureRenderTarget2D* MaskRT = NewObject<UTextureRenderTarget2D>(this);
		MaskRT->RenderTargetFormat = RTF_RGBA8;
		MaskRT->ClearColor = FLinearColor::Black;
		MaskRT->bAutoGenerateMips = false;
		MaskRT->InitAutoFormat(RtWidth, RtHeight); // sem supersample -- mascara nao precisa de AA extra
		MaskRT->UpdateResourceImmediate(true);

		MaskCaptureComponent->TextureTarget = MaskRT;
		MaskCaptureComponent->bIgnoreScreenPercentage = true;
		MaskCaptureComponent->bAlwaysPersistRenderingState = true;
	}
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
	//
	// Blur de rotacao da helice: TENTATIVA de religar so o motion blur
	// nativo aqui (MotionBlurAmount/MotionBlurMax + ShowFlags.SetMotionBlur)
	// NAO funcionou -- confirmado rodando o dataset de verdade, helice saia
	// sempre nitida independente dessas configuracoes. SceneCaptureComponent2D
	// nao processa o passe de motion blur do motor (limitacao conhecida da
	// engine). Solucao de verdade: ADroneCaptureController::CapturePropellerMotionBlur
	// (blur simulado por media de varios sub-frames, ver PropellerBlurSamples
	// no header do controller).
	DisableArtisticPostProcessEffects(Pps);

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
