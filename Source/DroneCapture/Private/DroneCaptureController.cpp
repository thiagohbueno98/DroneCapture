#include "DroneCaptureController.h"

#include "DroneCaptureCamera.h"
#include "DroneCaptureTarget.h"
#include "DroneCaptureGridVolume.h"
#include "DroneCaptureSetupWidget.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/EngineTypes.h"
#include "ImageUtils.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "RenderingThread.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace DroneCaptureInternal
{
	static TArray<float> RangeInclusive(float Start, float Stop, float Step)
	{
		TArray<float> Values;
		if (Step <= 0.0f)
		{
			Values.Add(Start);
			return Values;
		}
		float V = Start;
		while (V <= Stop + 1e-3f)
		{
			Values.Add(V);
			V += Step;
		}
		if (Values.Num() == 0)
		{
			Values.Add(Start);
		}
		return Values;
	}
}

ADroneCaptureController::ADroneCaptureController()
{
	PrimaryActorTick.bCanEverTick = true;
	SetupWidgetClass = UDroneCaptureSetupWidget::StaticClass();
}

void ADroneCaptureController::BeginPlay()
{
	Super::BeginPlay();

	if (bShowSetupMenuOnBeginPlay)
	{
		// Resolve a cena ANTES do menu -- o widget precisa de uma
		// referencia valida de Drone pra aplicar o modelo escolhido.
		ResolveSceneReferences();
		ShowSetupMenu();
	}
	else if (bAutoStartOnBeginPlay)
	{
		StartCapture();
	}
}

void ADroneCaptureController::ShowSetupMenu()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const TSubclassOf<UDroneCaptureSetupWidget> Cls = SetupWidgetClass ? SetupWidgetClass : TSubclassOf<UDroneCaptureSetupWidget>(UDroneCaptureSetupWidget::StaticClass());

	UE_LOG(LogTemp, Log, TEXT("[DroneCapture] ShowSetupMenu: PC=%s Cls=%s"), PC ? TEXT("ok") : TEXT("NULO"), *GetNameSafe(Cls.Get()));

	UDroneCaptureSetupWidget* Widget = (PC && Cls) ? CreateWidget<UDroneCaptureSetupWidget>(PC, Cls) : nullptr;
	if (!Widget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneCapture] Nao consegui montar o menu de setup -- iniciando captura direto com a config atual."));
		StartCapture();
		return;
	}

	Widget->Controller = this;
	Widget->AddToViewport(100);
	UE_LOG(LogTemp, Log, TEXT("[DroneCapture] Widget de setup adicionado ao viewport (IsInViewport=%s)."), Widget->IsInViewport() ? TEXT("true") : TEXT("false"));

	PC->bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(Widget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
}

FString ADroneCaptureController::GetEffectiveOutputDir() const
{
	return DatasetName.IsEmpty() ? OutputDir : FPaths::Combine(OutputDir, DatasetName);
}

void ADroneCaptureController::SetSunPreset(ESunPreset Preset)
{
	float Pitch = -85.0f;
	switch (Preset)
	{
	case ESunPreset::SolBaixo: Pitch = -30.0f; break;
	case ESunPreset::SolMedio: Pitch = -55.0f; break;
	case ESunPreset::SolAlto:  Pitch = -85.0f; break;
	}

	for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
	{
		ADirectionalLight* Light = *It;
		FRotator Rot = Light->GetActorRotation();
		Rot.Pitch = Pitch;
		Light->SetActorRotation(Rot);
		UE_LOG(LogTemp, Log, TEXT("[DroneCapture] Sol ajustado: Pitch=%.0f (%s)."), Pitch, *Light->GetName());
		break;
	}
}

void ADroneCaptureController::StartCapture()
{
	ResolveSceneReferences();
	BuildGridPoints();

	if (bApplyQualitySettingsOnStart)
	{
		ApplyQualitySettings();
	}

	PoseIndex = 0;
	WarmupFramesLeft = -1;
	DiscardCountByReason.Reset();

	const int32 YawCount = GetYawCount();
	bIsRunning = Drone != nullptr
		&& GridVolume != nullptr
		&& Cameras.Num() > 0
		&& GridPoints.Num() > 0
		&& YawCount > 0;

	if (!bIsRunning)
	{
		UE_LOG(LogTemp, Error, TEXT("[DroneCapture] Nao foi possivel iniciar a captura -- confira Drone/Cameras/GridVolume/YawAnglesDeg."));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[DroneCapture] Iniciando: %d posicoes x %d yaws x %d cameras = %d checagens."),
			GridPoints.Num(), YawCount, Cameras.Num(), GridPoints.Num() * YawCount * Cameras.Num());
	}
}

void ADroneCaptureController::StopCapture()
{
	bIsRunning = false;
}

float ADroneCaptureController::GetProgress() const
{
	const int32 Total = GridPoints.Num() * GetYawCount();
	if (Total <= 0)
	{
		return 0.0f;
	}
	return (float)PoseIndex / (float)Total;
}

void ADroneCaptureController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	SpinPropellers(DeltaSeconds);

	if (!bIsRunning)
	{
		return;
	}

	const int32 YawCount = GetYawCount();
	const int32 Total = GridPoints.Num() * YawCount;

	if (PoseIndex >= Total)
	{
		bIsRunning = false;
		WriteDataYaml();
		UE_LOG(LogTemp, Log, TEXT("[DroneCapture] Rodada concluida: %d poses processadas."), Total);
		return;
	}

	const int32 PointIndex = PoseIndex / YawCount;
	const int32 YawIdx = PoseIndex % YawCount;

	// Fase 1 (WarmupFramesLeft == -1): pose nova -- move o drone e agenda os
	// frames de aquecimento. A checagem de oclusao/bbox (via mascara, ver
	// CheckPoseFromMask) so acontece na Fase 3, depois de existir um frame
	// de verdade renderizado pra capturar.
	if (WarmupFramesLeft < 0)
	{
		const FVector Point = GridPoints[PointIndex];
		// bRandomYaw: sorteia um angulo novo pra cada pose (0-360) em vez de
		// varrer a lista fixa YawAnglesDeg -- cada (PointIndex,YawIdx) so e
		// visitado 1x (PoseIndex so anda pra frente), entao sortear aqui é
		// equivalente a ter pre-sorteado, sem risco de inconsistencia.
		const float Yaw = bRandomYaw ? FMath::FRandRange(0.0f, 360.0f) : YawAnglesDeg[YawIdx];

		Drone->SetActorLocation(Point);
		Drone->SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
		PendingYawDeg = Yaw;

		WarmupFramesLeft = FMath::Max(WarmupCaptures, 1);
		return; // deixa os proximos Ticks (reais) acumularem o historico temporal
	}

	// Fase 2 (WarmupFramesLeft > 0): 1 CaptureScene() por camera POR TICK
	// REAL -- e isso (nao um loop dentro do mesmo Tick) que deixa Lumen/
	// SSR/TAA convergirem de verdade (reflexo de agua etc. precisam de
	// varios frames reais passando na mesma pose).
	if (WarmupFramesLeft > 0)
	{
		for (int32 i = 0; i < Cameras.Num(); ++i)
		{
			if (USceneCaptureComponent2D* RgbComp = CameraComponents.IsValidIndex(i) ? CameraComponents[i] : nullptr)
			{
				RgbComp->CaptureScene();
			}
		}
		WarmupFramesLeft--;
		if (WarmupFramesLeft > 0)
		{
			return;
		}
		// WarmupFramesLeft chegou a 0 -- cai direto pra Fase 3 aqui embaixo,
		// no MESMO Tick, em vez de retornar e deixar a Fase 3 rodar so no
		// Tick seguinte. Achado rodando o dataset de verdade: SpinPropellers()
		// roda no TOPO de todo Tick (antes deste if), entao esperar mais um
		// Tick antes de capturar a mascara deixava a helice girar um pouco
		// mais entre o ultimo frame RGB (capturado acima) e a mascara -- a
		// ponta da pa aparecia visivelmente fora da bbox calculada a partir
		// da mascara (mascara e RGB nao eram mais do mesmo instante).
	}

	// Fase 3 (WarmupFramesLeft == 0): aquecimento da imagem RGB concluido --
	// captura a mascara (1 frame so, sem GI/Lumen, nao precisa de
	// aquecimento), le os pixels de volta e decide oclusao/bbox a partir do
	// que realmente apareceu. Exporta o RGB usando o TextureTarget
	// preenchido pelo ultimo CaptureScene() da fase 2 (nenhum
	// ExportSample/ExportDiscardDebug chama CaptureScene() de novo) -- a
	// nao ser que o blur de helice esteja ligado, ver
	// CapturePropellerMotionBlur/PendingBlurredRgbPixels.
	CapturePropellerMotionBlur();

	const FVector Point = GridPoints[PointIndex];
	for (int32 i = 0; i < Cameras.Num(); ++i)
	{
		AActor* CamActor = Cameras[i];
		USceneCaptureComponent2D* RgbComp = CameraComponents.IsValidIndex(i) ? CameraComponents[i] : nullptr;
		const ADroneCaptureCamera* CamCapture = Cast<ADroneCaptureCamera>(CamActor);
		USceneCaptureComponent2D* MaskComp = CamCapture ? CamCapture->GetMaskCaptureComponent() : nullptr;
		if (!CamActor || !RgbComp || !MaskComp)
		{
			continue;
		}

		MaskComp->CaptureScene();
		const FPoseCheckResult Result = CheckPoseFromMask(MaskComp);

		const FString CamLabel = FString::Printf(TEXT("CaptureCam%d"), i + 1);
		const FString SampleKey = FString::FromInt(PoseIndex);
		const int32 SupersampleFactor = CamCapture ? FMath::Max(1, CamCapture->SupersampleFactor) : 1;

		// "Drone nao aparece pra essa camera" -- nenhum pixel da mascara
		// detectado (oclusao total ou fora do campo de visao; a mascara nao
		// distingue os dois casos, e nao precisa -- bbox_pequena ainda tem
		// um drone de verdade visivel, so nao passou no criterio de
		// qualidade, entao nao conta como negativo aqui).
		const bool bDroneNotVisible = Result.Status == EPoseCheckStatus::Oclusao;

		if (Result.Status == EPoseCheckStatus::Ok)
		{
			ExportSample(CamLabel, SampleKey, RgbComp, Result.BboxMin, Result.BboxMax, Result.RtWidth, Result.RtHeight, SupersampleFactor);
		}
		else if (bDroneNotVisible && bSaveNegativeSamples && FMath::FRand() < NegativeSampleChance)
		{
			ExportNegativeSample(CamLabel, SampleKey, RgbComp, SupersampleFactor);
		}
		else if (bSaveDiscardDebug)
		{
			const FString Reason = StatusToReasonString(Result.Status);
			int32& Count = DiscardCountByReason.FindOrAdd(Reason);
			if (Count < DiscardDebugLimitPerReason)
			{
				Count++;
				const FString Info = FString::Printf(TEXT("pose_index=%d ponto=%s yaw=%.1f"), PoseIndex, *Point.ToString(), PendingYawDeg);
				ExportDiscardDebug(Reason, CamLabel, SampleKey, RgbComp, Info, SupersampleFactor);
			}
		}

		if (bSaveMaskDebug)
		{
			ExportMaskDebug(MaskComp, CamLabel, SampleKey);
		}
	}

	WarmupFramesLeft = -1;
	PoseIndex++;
}

// ============================================================
// Helices -- ver comentario no header sobre por que isso precisa ser
// nativo (girar_helices.py rodava no mundo do Editor, nao no mundo de
// Play usado pela captura de verdade).
// ============================================================

void ADroneCaptureController::SpinPropellers(float DeltaSeconds)
{
	if (!bSpinPropellers || !Drone)
	{
		return;
	}

	PropellerSpinAngleDeg = FMath::Fmod(PropellerSpinAngleDeg + PropellerSpinDegPerSec * DeltaSeconds, 360.0f);
	SetPropellerSpinAngle(PropellerSpinAngleDeg);
}

void ADroneCaptureController::SetPropellerSpinAngle(float AngleDeg)
{
	if (!Drone)
	{
		return;
	}

	// Cada modelo de drone tem sua propria geometria de helice (reta vs
	// inclinada) e precisa de um eixo de giro diferente -- se "Drone" for
	// um ADroneCaptureTarget, usa o eixo DELE (por-modelo); senao cai no
	// fallback fixo deste controller.
	const ADroneCaptureTarget* DroneTarget = Cast<ADroneCaptureTarget>(Drone);
	const EPropellerSpinAxis EffectiveSpinAxis = DroneTarget ? DroneTarget->PropellerSpinAxis : PropellerSpinAxis;

	for (UActorComponent* ActorComp : Drone->GetComponents())
	{
		UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(ActorComp);
		if (!Mesh)
		{
			continue;
		}

		const bool bNameMatches = !PropellerNameContains.IsEmpty() && Mesh->GetName().Contains(PropellerNameContains, ESearchCase::IgnoreCase);
		const bool bMeshMatches = Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetName().Equals(PropellerMeshName, ESearchCase::IgnoreCase);
		if (!bNameMatches && !bMeshMatches)
		{
			continue;
		}

		FRotator* BaseRotation = PropellerBaseRotations.Find(Mesh->GetFName());
		if (!BaseRotation)
		{
			BaseRotation = &PropellerBaseRotations.Add(Mesh->GetFName(), Mesh->GetRelativeRotation());
		}

		// Sentido de giro alternado por helice -- fisicamente as 2 helices
		// DIAGONAIS giram no mesmo sentido, entao as 2 do mesmo lado (par
		// adjacente) giram em sentidos opostos (evita torque resultante /
		// efeito giroscopico, como um quadricoptero real). ADroneCaptureTarget
		// sempre nomeia os componentes "Helice1".."Helice4" nessa ordem fixa
		// pra todos os 6 modelos, com Helice1/Helice2 num par diagonal e
		// Helice3/Helice4 no outro (conferido nos transforms de
		// GetModelDefinition() -- index0/1 tem X/Y de sinais opostos entre si,
		// e index2/3 idem). Helice1+Helice2 giram num sentido, Helice3+Helice4
		// no sentido contrario.
		const TCHAR LastChar = Mesh->GetName().Len() > 0 ? Mesh->GetName()[Mesh->GetName().Len() - 1] : TEXT('\0');
		const int32 PropNumber = FChar::IsDigit(LastChar) ? (LastChar - TEXT('0')) : 0;
		const float DirectionSign = (PropNumber == 3 || PropNumber == 4) ? -1.0f : 1.0f;

		// Compoe por quaternion (BaseQuat * SpinQuat) em vez de somar direto
		// num campo do FRotator -- soma direta so da certo quando o Pitch/Roll
		// base sao zero (helice reta). Quando a helice tem inclinacao base
		// (ex: DJI-Pro-Mini), somar Euler faz o giro sair em torno do eixo
		// mundial errado e ela "desencaixa" da inclinacao original. Compor os
		// quaternions gira em torno do PROPRIO eixo local da helice (aplicado
		// antes) e so depois aplica a inclinacao base por cima, mantendo a
		// helice sempre alinhada, so girando.
		const float SignedSpinAngleDeg = AngleDeg * DirectionSign;
		FRotator SpinDelta = FRotator::ZeroRotator;
		switch (EffectiveSpinAxis)
		{
		case EPropellerSpinAxis::Pitch: SpinDelta.Pitch = SignedSpinAngleDeg; break;
		case EPropellerSpinAxis::Yaw:   SpinDelta.Yaw   = SignedSpinAngleDeg; break;
		case EPropellerSpinAxis::Roll:  SpinDelta.Roll  = SignedSpinAngleDeg; break;
		}
		const FQuat NewQuat = BaseRotation->Quaternion() * SpinDelta.Quaternion();
		Mesh->SetRelativeRotation(NewQuat.Rotator());
	}
}

void ADroneCaptureController::CapturePropellerMotionBlur()
{
	PendingBlurredRgbPixels.Reset();

	if (PropellerBlurSamples <= 1)
	{
		return;
	}

	for (USceneCaptureComponent2D* RgbComp : CameraComponents)
	{
		if (!RgbComp || !RgbComp->TextureTarget)
		{
			continue;
		}

		const int32 Width = RgbComp->TextureTarget->SizeX;
		const int32 Height = RgbComp->TextureTarget->SizeY;
		if (Width <= 0 || Height <= 0)
		{
			continue;
		}

		TArray<FLinearColor> Accum;
		Accum.SetNumZeroed(Width * Height);

		// Varre de "PropellerSpinAngleDeg - Sweep" ate "PropellerSpinAngleDeg"
		// (a ULTIMA amostra bate exatamente com o angulo real final) -- box
		// filter no tempo, mesma ideia do supersampling espacial em
		// ExportCaptureToPng, so que aqui a media e entre FRAMES em vez de
		// entre TEXELS.
		for (int32 Sample = 0; Sample < PropellerBlurSamples; ++Sample)
		{
			const float T = (float)Sample / (float)(PropellerBlurSamples - 1);
			SetPropellerSpinAngle(PropellerSpinAngleDeg - PropellerBlurSweepDeg * (1.0f - T));

			RgbComp->CaptureScene();
			FlushRenderingCommands();

			FTextureRenderTargetResource* RTResource = RgbComp->TextureTarget->GameThread_GetRenderTargetResource();
			TArray<FColor> SubFramePixels;
			if (!RTResource || !RTResource->ReadPixels(SubFramePixels) || SubFramePixels.Num() != Accum.Num())
			{
				continue;
			}
			for (int32 PixelIdx = 0; PixelIdx < SubFramePixels.Num(); ++PixelIdx)
			{
				Accum[PixelIdx] += FLinearColor(SubFramePixels[PixelIdx]);
			}
		}

		// A mascara (capturada logo depois, por camera, no loop principal do
		// Tick) precisa bater com a ULTIMA posicao real da helice -- senao
		// volta o bug de "ponta da pa fora da bbox" ja corrigido antes. O
		// ultimo sample do loop acima ja deixa nesse angulo (T=1), mas
		// reforca explicito aqui pra nao depender dessa ordem por acidente.
		SetPropellerSpinAngle(PropellerSpinAngleDeg);

		const float InvSamples = 1.0f / (float)PropellerBlurSamples;
		TArray<FColor> Averaged;
		Averaged.SetNumUninitialized(Accum.Num());
		for (int32 PixelIdx = 0; PixelIdx < Accum.Num(); ++PixelIdx)
		{
			Averaged[PixelIdx] = (Accum[PixelIdx] * InvSamples).ToFColor(true);
		}

		PendingBlurredRgbPixels.Add(RgbComp, MoveTemp(Averaged));
	}
}

// ============================================================
// Descoberta de cena / grid (chamados 1x, nao sao fonte de vazamento)
// ============================================================

void ADroneCaptureController::ResolveSceneReferences()
{
	UWorld* World = GetWorld();

	// Descoberta por CLASSE (ADroneCaptureTarget/Camera/GridVolume) tem
	// prioridade -- cenas montadas 100% com os atores novos do plugin nao
	// precisam de Tag manual nenhuma. Se nao achar nada dessas classes,
	// cai no fallback por Tag (retrocompativel com cenas montadas por
	// setup_cena.py, como a da Park2).
	if (!Drone)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(World, ADroneCaptureTarget::StaticClass(), Found);
		if (Found.Num() > 0)
		{
			Drone = Found[0];
		}
		else
		{
			UGameplayStatics::GetAllActorsWithTag(World, FName(TEXT("DroneAlvo")), Found);
			if (Found.Num() > 0)
			{
				Drone = Found[0];
			}
		}
	}

	if (Cameras.Num() == 0)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(World, ADroneCaptureCamera::StaticClass(), Found);
		if (Found.Num() > 0)
		{
			Found.Sort([](const AActor& A, const AActor& B)
			{
				const ADroneCaptureCamera* CamA = Cast<ADroneCaptureCamera>(const_cast<AActor*>(&A));
				const ADroneCaptureCamera* CamB = Cast<ADroneCaptureCamera>(const_cast<AActor*>(&B));
				return (CamA ? CamA->CameraIndex : 0) < (CamB ? CamB->CameraIndex : 0);
			});
			Cameras = Found;
		}
		else
		{
			for (int32 i = 1; i <= 32; ++i)
			{
				TArray<AActor*> TagFound;
				UGameplayStatics::GetAllActorsWithTag(World, FName(*FString::Printf(TEXT("CaptureCam%d"), i)), TagFound);
				if (TagFound.Num() == 0)
				{
					break;
				}
				Cameras.Add(TagFound[0]);
			}
		}
	}

	if (!GridVolume)
	{
		TArray<AActor*> Found;
		UGameplayStatics::GetAllActorsOfClass(World, ADroneCaptureGridVolume::StaticClass(), Found);
		if (Found.Num() > 0)
		{
			GridVolume = Found[0];
		}
		else
		{
			UGameplayStatics::GetAllActorsWithTag(World, FName(TEXT("VolumeGrid")), Found);
			if (Found.Num() > 0)
			{
				GridVolume = Found[0];
			}
		}
	}

	CameraComponents.Reset();
	for (AActor* Cam : Cameras)
	{
		CameraComponents.Add(Cam ? Cam->FindComponentByClass<USceneCaptureComponent2D>() : nullptr);
	}

	// O Pawn "observador" que o usuario pilota durante o Play (ADefaultPawn,
	// default do GameModeBase) tem um MeshComponent de esfera marcado como
	// "visualization component" -- fica invisivel na viewport normal do PIE
	// (que so mostra esse tipo de componente no mundo do Editor), mas o
	// SceneCaptureComponent2D de cada camera de captura ainda o renderiza,
	// aparecendo como uma bola flutuando nas imagens exportadas. Escondido
	// explicitamente via HiddenActors, que funciona independente desse
	// detalhe.
	if (APawn* ObserverPawn = UGameplayStatics::GetPlayerPawn(World, 0))
	{
		for (USceneCaptureComponent2D* CamComp : CameraComponents)
		{
			if (CamComp)
			{
				CamComp->HiddenActors.AddUnique(ObserverPawn);
			}
		}
	}

	ConfigureDroneMask();

	// Aquecimento do shader do M_DroneMask -- achado rodando o dataset de
	// verdade pela primeira vez: a PRIMEIRA CaptureScene() de cada
	// MaskComponent, logo no inicio do Play, pode cair num frame onde o
	// shader do material de post-process ainda esta compilando (PSO/shader
	// assincrono), renderizando a cena RGB normal em vez da mascara preto/
	// branco -- ComputeMaskBbox entao interpreta a cena inteira como "drone"
	// e salva bbox=imagem inteira (visto em CaptureCam1_0, pose_index=0, a
	// unica amostra corrompida em 525). Capturas descartadas aqui (antes do
	// loop de poses comecar) forcam essa compilacao a acontecer fora da
	// amostra de verdade.
	for (AActor* CamActor : Cameras)
	{
		const ADroneCaptureCamera* CamCapture = Cast<ADroneCaptureCamera>(CamActor);
		if (USceneCaptureComponent2D* MaskComp = CamCapture ? CamCapture->GetMaskCaptureComponent() : nullptr)
		{
			MaskComp->CaptureScene();
			MaskComp->CaptureScene();
		}
	}

	if (!Drone)
	{
		UE_LOG(LogTemp, Error, TEXT("[DroneCapture] Drone nao encontrado (referencia vazia e nenhum ator com tag 'DroneAlvo')."));
	}
	if (!GridVolume)
	{
		UE_LOG(LogTemp, Error, TEXT("[DroneCapture] GridVolume nao encontrado (referencia vazia e nenhum ator com tag 'VolumeGrid')."));
	}
	if (Cameras.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[DroneCapture] Nenhuma camera encontrada (referencia vazia e nenhum ator com tag 'CaptureCam1'..)."));
	}
}

void ADroneCaptureController::BuildGridPoints()
{
	GridPoints.Reset();

	if (!GridVolume)
	{
		return;
	}

	// Esconde o VolumeGrid das capturas e desliga a colisao dele -- senao o
	// proprio volume bloqueia o raycast de oclusao (achado ja documentado
	// no fluxo Python/Blueprint).
	for (UActorComponent* ActorComp : GridVolume->GetComponents())
	{
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(ActorComp))
		{
			Prim->bHiddenInSceneCapture = true;
		}
	}
	GridVolume->SetActorEnableCollision(false);

	FVector Origin, Extent;
	GridVolume->GetActorBounds(false, Origin, Extent);

	const TArray<float> Xs = DroneCaptureInternal::RangeInclusive(Origin.X - Extent.X, Origin.X + Extent.X, GridStepXM * 100.0f);
	const TArray<float> Ys = DroneCaptureInternal::RangeInclusive(Origin.Y - Extent.Y, Origin.Y + Extent.Y, GridStepYM * 100.0f);
	const TArray<float> Zs = DroneCaptureInternal::RangeInclusive(Origin.Z - Extent.Z, Origin.Z + Extent.Z, GridStepZM * 100.0f);

	GridPoints.Reserve(Xs.Num() * Ys.Num() * Zs.Num());
	for (float X : Xs)
	{
		for (float Y : Ys)
		{
			for (float Z : Zs)
			{
				GridPoints.Add(FVector(X, Y, Z));
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[DroneCapture] Grid: %d x %d x %d = %d posicoes"), Xs.Num(), Ys.Num(), Zs.Num(), GridPoints.Num());
}

void ADroneCaptureController::ApplyQualitySettings()
{
	static const TCHAR* ScalabilityCVars[] = {
		TEXT("sg.ViewDistanceQuality"), TEXT("sg.AntiAliasingQuality"), TEXT("sg.ShadowQuality"),
		TEXT("sg.GlobalIlluminationQuality"), TEXT("sg.ReflectionQuality"), TEXT("sg.PostProcessQuality"),
		TEXT("sg.TextureQuality"), TEXT("sg.EffectsQuality"), TEXT("sg.FoliageQuality"), TEXT("sg.ShadingQuality"),
	};

	UWorld* World = GetWorld();
	for (const TCHAR* CVar : ScalabilityCVars)
	{
		UKismetSystemLibrary::ExecuteConsoleCommand(World, FString::Printf(TEXT("%s 3"), CVar));
	}
	UKismetSystemLibrary::ExecuteConsoleCommand(World, TEXT("r.Streaming.FullyLoadUsedTextures 1"));
	UKismetSystemLibrary::ExecuteConsoleCommand(World, TEXT("r.Shadow.Virtual.Enable 1"));
	UKismetSystemLibrary::ExecuteConsoleCommand(World, TEXT("r.Shadow.MaxResolution 4096"));

	UE_LOG(LogTemp, Log, TEXT("[DroneCapture] Configuracoes de qualidade grafica aplicadas."));
}

void ADroneCaptureController::WriteDataYaml() const
{
	const FString EffectiveDir = GetEffectiveOutputDir();
	IFileManager::Get().MakeDirectory(*EffectiveDir, true);
	const FString YamlPath = FPaths::Combine(EffectiveDir, TEXT("data.yaml"));
	const FString Content = FString::Printf(
		TEXT("path: %s\ntrain: images/train\nval: images/val\nnc: 1\nnames: [\"drone\"]\n"),
		*EffectiveDir);
	FFileHelper::SaveStringToFile(Content, *YamlPath);
}

// ============================================================
// Mascara de segmentacao (equivalente a capture_core.py, mas por pixel
// de verdade em vez de raycast+projecao geometrica -- ver comentario em
// EdgeMarginFraction/MaskStencilValue no header pro porque da mudanca)
// ============================================================

void ADroneCaptureController::ConfigureDroneMask()
{
	if (!Drone)
	{
		return;
	}

	// r.CustomDepth precisa estar em 3 ("Enabled with Stencil") pro
	// CustomStencil funcionar -- o default da ENGINE e 1 ("enabled, sem
	// stencil"), e nenhum projeto novo vem com isso configurado (achado
	// rodando de verdade: com r.CustomDepth=1, CustomStencil sai sempre
	// 0/preto em QUALQUER objeto, nao importa o que configure nos
	// componentes -- ver contexto.md). Forcado aqui via console command
	// pra funcionar em qualquer projeto novo sem passo manual nenhum
	// (documentar no README nao seria suficiente -- usuario esqueceria).
	if (UWorld* World = GetWorld())
	{
		UKismetSystemLibrary::ExecuteConsoleCommand(World, TEXT("r.CustomDepth 3"));
	}

	// TODOS os componentes visuais do drone (nao so os com colisao -- aqui
	// queremos a silhueta VISIVEL de verdade, colisao nao importa mais).
	//
	// Tentativa abandonada (2026-09-17): marcar TODO O RESTO DA CENA com
	// CustomDepth tambem (pra deixar o teste de profundidade do drone
	// contra oclusores usar o Z-test interno do proprio CustomDepth, sem
	// comparar 2 buffers diferentes) -- funcionava em teoria, mas quebrou a
	// captura de verdade no CityPark (0 amostras salvas, mascara saindo
	// toda branca). Causa: malhas Nanite (a maior parte do nivel) forcam um
	// fallback pra renderizacao NAO-Nanite quando marcadas CustomDepth em
	// massa (confirmado pelo aviso "[VSM] Sobrecarga da fila de marcacao
	// que nao sao do Nanite" no log), mesmo com r.Nanite.CustomDepth=1 (ja
	// vem ligado por padrao no motor -- nao era isso). Nao escala pra
	// niveis grandes/Nanite-pesados. Voltado pra so marcar o drone, com
	// oclusao resolvida via comparacao de profundidade em CheckPoseFromMask
	// (ver DisableTemporalJitterForMask -- desligar TAA/TSR na captura da
	// mascara elimina o ruido que motivou a tentativa de marcar tudo).
	for (UActorComponent* ActorComp : Drone->GetComponents())
	{
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(ActorComp))
		{
			Prim->SetRenderCustomDepth(true);
			Prim->SetCustomDepthStencilValue(MaskStencilValue);
		}
	}
}

bool ADroneCaptureController::ComputeMaskBbox(UTextureRenderTarget2D* MaskTarget, FVector2D& OutMin, FVector2D& OutMax, int32& OutVisiblePixelCount) const
{
	OutVisiblePixelCount = 0;

	if (!MaskTarget)
	{
		return false;
	}

	FTextureRenderTargetResource* RTResource = MaskTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		return false;
	}

	TArray<FColor> Pixels;
	if (!RTResource->ReadPixels(Pixels) || Pixels.Num() <= 0)
	{
		return false;
	}

	const int32 Width = MaskTarget->SizeX;
	const int32 Height = MaskTarget->SizeY;
	if (Pixels.Num() != Width * Height)
	{
		return false;
	}

	int32 MinX = Width, MaxX = -1, MinY = Height, MaxY = -1;
	int32 PixelCount = 0;
	for (int32 Y = 0; Y < Height; ++Y)
	{
		const int32 RowOffset = Y * Width;
		for (int32 X = 0; X < Width; ++X)
		{
			const FColor& Pixel = Pixels[RowOffset + X];
			if (Pixel.R > MaskPixelThreshold || Pixel.G > MaskPixelThreshold || Pixel.B > MaskPixelThreshold)
			{
				MinX = FMath::Min(MinX, X);
				MaxX = FMath::Max(MaxX, X);
				MinY = FMath::Min(MinY, Y);
				MaxY = FMath::Max(MaxY, Y);
				PixelCount++;
			}
		}
	}

	if (MaxX < MinX || MaxY < MinY)
	{
		return false; // nenhum pixel de drone encontrado
	}

	OutMin = FVector2D((float)MinX, (float)MinY);
	OutMax = FVector2D((float)(MaxX + 1), (float)(MaxY + 1));
	OutVisiblePixelCount = PixelCount;
	return true;
}

EPoseCheckStatus ADroneCaptureController::BboxQualityReason(const FVector2D& Min, const FVector2D& Max, int32 Width, int32 Height, int32 VisiblePixelCount) const
{
	if (VisiblePixelCount < MinDronePixelCount)
	{
		return EPoseCheckStatus::BboxPequena;
	}

	const float Area = (Max.X - Min.X) * (Max.Y - Min.Y);
	if (Area > (float)(Width * Height) * MaxBboxAreaFraction)
	{
		return EPoseCheckStatus::BboxMuitoGrande;
	}

	const float MarginX = Width * EdgeMarginFraction;
	const float MarginY = Height * EdgeMarginFraction;
	if (Min.X < MarginX || Min.Y < MarginY || Max.X > Width - MarginX || Max.Y > Height - MarginY)
	{
		return EPoseCheckStatus::BboxNaBorda;
	}

	return EPoseCheckStatus::Ok;
}

FPoseCheckResult ADroneCaptureController::CheckPoseFromMask(USceneCaptureComponent2D* MaskComp) const
{
	FPoseCheckResult Result;

	if (!MaskComp || !MaskComp->TextureTarget)
	{
		Result.Status = EPoseCheckStatus::Oclusao;
		return Result;
	}

	// FlushRenderingCommands() de verdade -- CaptureScene() so enfileira o
	// trabalho na render thread, precisa esperar terminar antes de ler os
	// pixels de volta (mesmo motivo do ExportCaptureToPng()).
	FlushRenderingCommands();

	const int32 Width = MaskComp->TextureTarget->SizeX;
	const int32 Height = MaskComp->TextureTarget->SizeY;

	FVector2D BboxMin, BboxMax;
	int32 VisiblePixelCount = 0;
	if (!ComputeMaskBbox(MaskComp->TextureTarget, BboxMin, BboxMax, VisiblePixelCount))
	{
		Result.Status = EPoseCheckStatus::Oclusao; // nenhum pixel de drone -- oculto ou fora de campo, tanto faz
		return Result;
	}

	Result.Status = BboxQualityReason(BboxMin, BboxMax, Width, Height, VisiblePixelCount);
	Result.BboxMin = BboxMin;
	Result.BboxMax = BboxMax;
	Result.RtWidth = Width;
	Result.RtHeight = Height;
	return Result;
}

// ============================================================
// Exportacao (equivalente a export_sample/export_discard_debug)
// ============================================================

void ADroneCaptureController::ExportSample(const FString& CamLabel, const FString& SampleKey, USceneCaptureComponent2D* RgbComp, const FVector2D& BboxMin, const FVector2D& BboxMax, int32 RtWidth, int32 RtHeight, int32 SupersampleFactor) const
{
	if (!RgbComp || !RgbComp->TextureTarget || RtWidth <= 0 || RtHeight <= 0)
	{
		return;
	}

	const FString EffectiveDir = GetEffectiveOutputDir();
	const FString ImagesDir = FPaths::Combine(EffectiveDir, TEXT("images"), Split);
	const FString LabelsDir = FPaths::Combine(EffectiveDir, TEXT("labels"), Split);
	IFileManager::Get().MakeDirectory(*ImagesDir, true);
	IFileManager::Get().MakeDirectory(*LabelsDir, true);

	const FString BaseName = FString::Printf(TEXT("%s_%s"), *CamLabel, *SampleKey);

	ExportCaptureToPng(RgbComp, SupersampleFactor, ImagesDir, BaseName + TEXT(".png"));

	// Bbox foi calculada em pixels na resolucao de CAPTURA (RtWidth/RtHeight
	// aqui sao os do TextureTarget bruto, ja maiores quando SupersampleFactor
	// > 1) -- a normalizacao 0-1 do formato YOLO da exatamente o mesmo
	// resultado nessa resolucao ou na final reduzida (a razao bbox/imagem nao
	// muda), entao nao precisa recalcular nada pra combinar com o PNG
	// reduzido que ExportCaptureToPng acabou de salvar.
	const float Xc = (BboxMin.X + BboxMax.X) / 2.0f / RtWidth;
	const float Yc = (BboxMin.Y + BboxMax.Y) / 2.0f / RtHeight;
	const float W = (BboxMax.X - BboxMin.X) / RtWidth;
	const float H = (BboxMax.Y - BboxMin.Y) / RtHeight;

	const FString LabelPath = FPaths::Combine(LabelsDir, BaseName + TEXT(".txt"));
	const FString LabelContent = FString::Printf(TEXT("0 %.6f %.6f %.6f %.6f\n"), Xc, Yc, W, H);
	FFileHelper::SaveStringToFile(LabelContent, *LabelPath);
}

void ADroneCaptureController::ExportNegativeSample(const FString& CamLabel, const FString& SampleKey, USceneCaptureComponent2D* RgbComp, int32 SupersampleFactor) const
{
	if (!RgbComp || !RgbComp->TextureTarget)
	{
		return;
	}

	const FString EffectiveDir = GetEffectiveOutputDir();
	const FString ImagesDir = FPaths::Combine(EffectiveDir, TEXT("images"), Split);
	const FString LabelsDir = FPaths::Combine(EffectiveDir, TEXT("labels"), Split);
	IFileManager::Get().MakeDirectory(*ImagesDir, true);
	IFileManager::Get().MakeDirectory(*LabelsDir, true);

	const FString BaseName = FString::Printf(TEXT("%s_%s"), *CamLabel, *SampleKey);

	ExportCaptureToPng(RgbComp, SupersampleFactor, ImagesDir, BaseName + TEXT(".png"));

	// Label VAZIO -- convencao YOLO pra "amostra negativa" (imagem sem
	// nenhum objeto da classe). Cria o arquivo mesmo vazio (em vez de nao
	// criar nenhum) pra deixar explicito que a ausencia de bbox e
	// intencional, nao uma amostra que faltou processar.
	const FString LabelPath = FPaths::Combine(LabelsDir, BaseName + TEXT(".txt"));
	FFileHelper::SaveStringToFile(TEXT(""), *LabelPath);
}

void ADroneCaptureController::ExportDiscardDebug(const FString& Reason, const FString& CamLabel, const FString& SampleKey, USceneCaptureComponent2D* RgbComp, const FString& InfoText, int32 SupersampleFactor) const
{
	if (!RgbComp || !RgbComp->TextureTarget)
	{
		return;
	}

	const FString OutDir = FPaths::Combine(GetEffectiveOutputDir(), TEXT("debug_descartados"), Reason);
	IFileManager::Get().MakeDirectory(*OutDir, true);

	const FString BaseName = FString::Printf(TEXT("%s_%s"), *CamLabel, *SampleKey);

	ExportCaptureToPng(RgbComp, SupersampleFactor, OutDir, BaseName + TEXT(".png"));

	FFileHelper::SaveStringToFile(InfoText + TEXT("\n"), *FPaths::Combine(OutDir, BaseName + TEXT(".txt")));
}

void ADroneCaptureController::ExportMaskDebug(USceneCaptureComponent2D* MaskComp, const FString& CamLabel, const FString& SampleKey) const
{
	if (!MaskComp || !MaskComp->TextureTarget)
	{
		return;
	}

	const FString OutDir = FPaths::Combine(GetEffectiveOutputDir(), TEXT("debug_mascara"));
	IFileManager::Get().MakeDirectory(*OutDir, true);

	const FString BaseName = FString::Printf(TEXT("%s_%s_mask"), *CamLabel, *SampleKey);

	FlushRenderingCommands();
	UKismetRenderingLibrary::ExportRenderTarget(const_cast<ADroneCaptureController*>(this), MaskComp->TextureTarget, OutDir, BaseName + TEXT(".png"));
}

void ADroneCaptureController::ExportCaptureToPng(USceneCaptureComponent2D* RgbComp, int32 SupersampleFactor, const FString& OutDir, const FString& FileName) const
{
	if (!RgbComp || !RgbComp->TextureTarget)
	{
		return;
	}

	const int32 SrcWidth = RgbComp->TextureTarget->SizeX;
	const int32 SrcHeight = RgbComp->TextureTarget->SizeY;

	// Se o blur de helice esta ligado (ver CapturePropellerMotionBlur), usa
	// o buffer JA MEDIADO entre varios sub-frames em vez de reler a GPU --
	// a textura em si so tem o ULTIMO sub-frame capturado (nitido, sem
	// blur nenhum).
	TArray<FColor> FreshPixels;
	const TArray<FColor>* SrcPixels = PendingBlurredRgbPixels.Find(RgbComp);

	if (!SrcPixels)
	{
		// FlushRenderingCommands() de verdade (funcao de engine), nao o hack
		// de console command usado no fluxo Python/Blueprint (que nao
		// funcionava em Play mode -- ver contexto.md).
		FlushRenderingCommands();

		if (SupersampleFactor <= 1)
		{
			UKismetRenderingLibrary::ExportRenderTarget(const_cast<ADroneCaptureController*>(this), RgbComp->TextureTarget, OutDir, FileName);
			return;
		}

		FTextureRenderTargetResource* RTResource = RgbComp->TextureTarget->GameThread_GetRenderTargetResource();
		if (!RTResource || !RTResource->ReadPixels(FreshPixels) || FreshPixels.Num() <= 0)
		{
			return;
		}
		SrcPixels = &FreshPixels;
	}

	if (SrcPixels->Num() != SrcWidth * SrcHeight)
	{
		return;
	}

	if (SupersampleFactor <= 1)
	{
		// Sem supersample mas COM blur (cache preenchido): precisa compactar
		// na unha -- o atalho ExportRenderTarget so le direto da GPU, nao de
		// um buffer ja em memoria.
		TArray64<uint8> CompressedPng;
		FImageUtils::PNGCompressImageArray(SrcWidth, SrcHeight, TArrayView64<const FColor>(SrcPixels->GetData(), SrcPixels->Num()), CompressedPng);
		IFileManager::Get().MakeDirectory(*OutDir, true);
		const FString FilePath = FPaths::Combine(OutDir, FileName);
		FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(CompressedPng.GetData(), CompressedPng.Num()), *FilePath);
		return;
	}

	// Supersampling ligado (ver ADroneCaptureCamera::SupersampleFactor): o
	// TextureTarget foi renderizado SupersampleFactor vezes maior de
	// proposito. Reduz aqui por "box filter" de verdade -- le os pixels
	// brutos da GPU (ou o buffer ja mediado do blur, ver acima), converte
	// cada um pra espaco LINEAR (FLinearColor a partir de FColor ja faz a
	// conversao sRGB->linear), calcula a MEDIA dos SupersampleFactor x
	// SupersampleFactor texels de cada pixel final, e so converte de volta
	// pra sRGB no final. Fazer a media em espaco linear (em vez de so
	// redimensionar a imagem grande com filtragem bilinear de 1 amostra) e
	// o que de fato reduz ruido/serrilhado (mesma ideia de SSAA).
	const int32 DstWidth = SrcWidth / SupersampleFactor;
	const int32 DstHeight = SrcHeight / SupersampleFactor;
	if (DstWidth <= 0 || DstHeight <= 0)
	{
		return;
	}

	TArray<FColor> DstPixels;
	DstPixels.SetNumUninitialized(DstWidth * DstHeight);

	const float InvSampleCount = 1.0f / (float)(SupersampleFactor * SupersampleFactor);
	for (int32 DstY = 0; DstY < DstHeight; ++DstY)
	{
		const int32 SrcY0 = DstY * SupersampleFactor;
		for (int32 DstX = 0; DstX < DstWidth; ++DstX)
		{
			const int32 SrcX0 = DstX * SupersampleFactor;
			FLinearColor Sum = FLinearColor::Black;
			for (int32 SubY = 0; SubY < SupersampleFactor; ++SubY)
			{
				const int32 RowOffset = (SrcY0 + SubY) * SrcWidth;
				for (int32 SubX = 0; SubX < SupersampleFactor; ++SubX)
				{
					Sum += FLinearColor((*SrcPixels)[RowOffset + SrcX0 + SubX]);
				}
			}
			DstPixels[DstY * DstWidth + DstX] = (Sum * InvSampleCount).ToFColor(true);
		}
	}

	TArray64<uint8> CompressedPng;
	FImageUtils::PNGCompressImageArray(DstWidth, DstHeight, TArrayView64<const FColor>(DstPixels.GetData(), DstPixels.Num()), CompressedPng);

	IFileManager::Get().MakeDirectory(*OutDir, true);
	const FString FilePath = FPaths::Combine(OutDir, FileName);
	FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(CompressedPng.GetData(), CompressedPng.Num()), *FilePath);
}

FString ADroneCaptureController::StatusToReasonString(EPoseCheckStatus Status)
{
	switch (Status)
	{
	case EPoseCheckStatus::Oclusao: return TEXT("oclusao");
	case EPoseCheckStatus::ForaDoCampoDeVisao: return TEXT("fora_do_campo_de_visao");
	case EPoseCheckStatus::BboxPequena: return TEXT("bbox_pequena");
	case EPoseCheckStatus::BboxNaBorda: return TEXT("bbox_na_borda");
	case EPoseCheckStatus::BboxMuitoGrande: return TEXT("bbox_muito_grande");
	default: return TEXT("ok");
	}
}
