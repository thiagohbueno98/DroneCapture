#include "DroneCaptureTarget.h"

#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Um modelo por pasta em /DroneCapture/Meshes/<Folder>/ -- todos usam
	// "Quadrotor1"/"Propeller" como nome de asset (pacote e objeto), sem
	// excecao (o DJI-TELLO teve o objeto renomeado pra "Quadrotor1" pra
	// ficar consistente com os outros modelos).
	struct FDroneModelDefinition
	{
		FString Folder;
		TArray<FTransform> PropellerTransforms;
		// Yaw validado ao vivo pra todos os 6 modelos em 2026-09-16 (ver
		// comentario no header) -- parametro deixado ajustavel por modelo
		// mesmo assim, caso um modelo futuro precise de outro eixo.
		EPropellerSpinAxis SpinAxis = EPropellerSpinAxis::Yaw;
	};

	FDroneModelDefinition MakeDefinition(const FString& Folder, TArray<FTransform> PropellerTransforms, EPropellerSpinAxis SpinAxis = EPropellerSpinAxis::Yaw)
	{
		FDroneModelDefinition Def;
		Def.Folder = Folder;
		Def.PropellerTransforms = MoveTemp(PropellerTransforms);
		Def.SpinAxis = SpinAxis;
		return Def;
	}

	// Transforms de helice EXATOS por modelo (relativos ao Corpo), tirados
	// em 2026-09-16 via diagnosticar_helices_todos_modelos.py rodado em
	// cima do BP_FlyingPawn_<Modelo> de cada um (pacote AirSim original,
	// consertado pelo usuario). Cada modelo tem geometria de braco propria
	// -- os 4 valores de um nao servem pra outro.
	const FDroneModelDefinition& GetModelDefinition(EDroneModel Model)
	{
		static const FDroneModelDefinition DJIProMini = MakeDefinition(TEXT("DJI-Pro-Mini"), {
			FTransform(FRotator(-16.0f, 0.0f, 15.0f), FVector(-43.3f, 40.7f, 11.5f)),
			FTransform(FRotator(-15.0f, 0.0f, -8.0f), FVector(31.6f, -55.5f, -1.5f)),
			FTransform(FRotator(-15.0f, 0.0f, 8.0f), FVector(31.6f, 55.4f, -1.5f)),
			FTransform(FRotator(-15.0f, 0.0f, -9.0f), FVector(-43.2f, -40.7f, 12.0f)),
		}); // Yaw -- validado ao vivo em 2026-09-16 (Roll estava errado apesar
			// do achado antigo do Pivo 2, provavelmente por uma malha/transform
			// diferente desde entao)

		static const FDroneModelDefinition DJI350RTK = MakeDefinition(TEXT("DJI-350RTK"), {
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(-70.0f, 60.0f, 59.0f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(82.0f, -55.0f, 59.0f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(82.0f, 55.0f, 59.0f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(-70.0f, -60.0f, 59.0f)),
		});

		static const FDroneModelDefinition DJINeo = MakeDefinition(TEXT("DJI-Neo"), {
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(-21.0f, 28.0f, 0.1f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(18.0f, -29.0f, 0.1f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(18.0f, 28.0f, 0.1f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(-21.0f, -29.0f, 0.1f)),
		});

		static const FDroneModelDefinition DJIPhantom = MakeDefinition(TEXT("DJI-Phantom"), {
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(-59.0f, 51.0f, 110.0f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(48.0f, -55.0f, 110.0f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(48.0f, 51.0f, 110.0f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(-60.0f, -54.0f, 110.0f)),
		});

		static const FDroneModelDefinition DJITello = MakeDefinition(TEXT("DJI-TELLO"), {
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(-65.0f, 45.0f, 17.5f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(21.0f, -45.0f, 17.5f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(21.0f, 45.0f, 17.5f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(-65.0f, -45.0f, 17.5f)),
		});

		// Pawn generico/padrao do AirSim (BP_FlyingPawn sem sufixo) -- malha
		// PROPRIA (Content/Models/QuadRotor1 no projeto original, migrada
		// pra /DroneCapture/Meshes/AirSim-Default/), diferente da malha do
		// DJIProMini apesar do mesmo nome de arquivo "Quadrotor1".
		static const FDroneModelDefinition AirSimDefault = MakeDefinition(TEXT("AirSim-Default"), {
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(-25.0f, 25.0f, 0.1f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(25.0f, -25.0f, 0.1f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(25.0f, 25.0f, 0.1f)),
			FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(-25.0f, -25.0f, 0.1f)),
		});

		switch (Model)
		{
		case EDroneModel::DJIProMini:    return DJIProMini;
		case EDroneModel::DJI350RTK:     return DJI350RTK;
		case EDroneModel::DJINeo:        return DJINeo;
		case EDroneModel::DJIPhantom:    return DJIPhantom;
		case EDroneModel::DJITello:      return DJITello;
		case EDroneModel::AirSimDefault: return AirSimDefault;
		default:                         return DJIProMini;
		}
	}
}

ADroneCaptureTarget::ADroneCaptureTarget()
{
	PrimaryActorTick.bCanEverTick = false;

	Corpo = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Corpo"));
	RootComponent = Corpo;

	// Malha bundlada dentro do proprio Content do plugin (ver
	// migrar_modelos_drone_pro_plugin.py) -- vem como default, mas
	// continua ajustavel por instancia/projeto via BodyMesh/PropellerMesh
	// no Details panel se quiser um modelo de drone diferente.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultBodyMeshFinder(TEXT("/DroneCapture/Meshes/DJI-Pro-Mini/Quadrotor1.Quadrotor1"));
	if (DefaultBodyMeshFinder.Succeeded())
	{
		BodyMesh = DefaultBodyMeshFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultPropellerMeshFinder(TEXT("/DroneCapture/Meshes/DJI-Pro-Mini/Propeller.Propeller"));
	if (DefaultPropellerMeshFinder.Succeeded())
	{
		PropellerMesh = DefaultPropellerMeshFinder.Object;
	}

	// Default inicial -- ApplyMeshes()/OnConstruction sobrescreve com a
	// tabela por-modelo assim que roda (DroneModel != Custom).
	PropellerTransforms = GetModelDefinition(EDroneModel::DJIProMini).PropellerTransforms;

	static const TCHAR* PropellerNames[] = { TEXT("Helice1"), TEXT("Helice2"), TEXT("Helice3"), TEXT("Helice4") };
	for (int32 i = 0; i < 4; ++i)
	{
		UStaticMeshComponent* Prop = CreateDefaultSubobject<UStaticMeshComponent>(PropellerNames[i]);
		Prop->SetupAttachment(Corpo);
		if (PropellerTransforms.IsValidIndex(i))
		{
			Prop->SetRelativeTransform(PropellerTransforms[i]);
		}
		Propellers.Add(Prop);
	}

	Tags.Add(TEXT("DroneAlvo"));
}

void ADroneCaptureTarget::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyMeshes();
}

void ADroneCaptureTarget::SetDroneModel(EDroneModel NewModel)
{
	DroneModel = NewModel;
	ApplyMeshes();
}

bool ADroneCaptureTarget::ResolveDroneModelData(EDroneModel Model, UStaticMesh*& OutBody, UStaticMesh*& OutPropeller, TArray<FTransform>& OutPropellerTransforms, EPropellerSpinAxis& OutSpinAxis) const
{
	if (Model == EDroneModel::Custom)
	{
		return false; // nao mexe em BodyMesh/PropellerMesh/PropellerTransforms/PropellerSpinAxis
	}

	const FDroneModelDefinition& Def = GetModelDefinition(Model);
	const FString BasePath = FString::Printf(TEXT("/DroneCapture/Meshes/%s/"), *Def.Folder);
	OutBody = LoadObject<UStaticMesh>(nullptr, *(BasePath + TEXT("Quadrotor1.Quadrotor1")));
	OutPropeller = LoadObject<UStaticMesh>(nullptr, *(BasePath + TEXT("Propeller.Propeller")));
	OutPropellerTransforms = Def.PropellerTransforms;
	OutSpinAxis = Def.SpinAxis;

	if (!OutBody || !OutPropeller)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneCapture] Nao encontrei a malha do modelo '%s' em %s -- rodou migrar_modelos_drone_pro_plugin.py?"), *Def.Folder, *BasePath);
	}
	return true;
}

void ADroneCaptureTarget::ApplyMeshes()
{
	UStaticMesh* ResolvedBody = nullptr;
	UStaticMesh* ResolvedPropeller = nullptr;
	TArray<FTransform> ResolvedPropellerTransforms;
	EPropellerSpinAxis ResolvedSpinAxis = PropellerSpinAxis;
	if (ResolveDroneModelData(DroneModel, ResolvedBody, ResolvedPropeller, ResolvedPropellerTransforms, ResolvedSpinAxis))
	{
		// DroneModel != Custom -- a escolha do dropdown manda, sobrescreve
		// qualquer BodyMesh/PropellerMesh/PropellerTransforms/PropellerSpinAxis
		// atribuido na mao antes.
		if (ResolvedBody)
		{
			BodyMesh = ResolvedBody;
		}
		if (ResolvedPropeller)
		{
			PropellerMesh = ResolvedPropeller;
		}
		if (ResolvedPropellerTransforms.Num() > 0)
		{
			PropellerTransforms = ResolvedPropellerTransforms;
		}
		PropellerSpinAxis = ResolvedSpinAxis;
	}

	if (Corpo && BodyMesh)
	{
		Corpo->SetStaticMesh(BodyMesh);
	}

	for (int32 i = 0; i < Propellers.Num(); ++i)
	{
		if (!Propellers[i])
		{
			continue;
		}
		if (PropellerMesh)
		{
			Propellers[i]->SetStaticMesh(PropellerMesh);
		}
		if (PropellerTransforms.IsValidIndex(i))
		{
			Propellers[i]->SetRelativeTransform(PropellerTransforms[i]);
		}
	}
}
