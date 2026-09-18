#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DroneCaptureSetupWidget.generated.h"

class ADroneCaptureController;
class UComboBoxString;
class UEditableTextBox;
class UButton;
class UCheckBox;
class UWidget;

/**
 * Menu de configuracao mostrado ao apertar Play (antes da captura
 * comecar) -- monta a propria UI em C++ (NativeOnInitialized), sem
 * precisar de Widget Blueprint nenhum. Deixado 100% override-avel: se
 * quiser um visual melhor, crie uma Widget Blueprint filha desta classe e
 * aponte ADroneCaptureController::SetupWidgetClass pra ela.
 */
UCLASS()
class DRONECAPTURE_API UDroneCaptureSetupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UDroneCaptureSetupWidget(const FObjectInitializer& ObjectInitializer);

	UPROPERTY(BlueprintReadWrite, Category = "Drone Capture")
	ADroneCaptureController* Controller = nullptr;

protected:
	// Monta a arvore de widgets aqui (NAO em NativeConstruct) -- o Unreal
	// chama RebuildWidget()/TakeWidget() ANTES de NativeConstruct, entao
	// WidgetTree->RootWidget precisa existir mais cedo, senao o widget
	// entra no viewport com conteudo vazio (sem erro nenhum, so nada
	// aparece -- gotcha achado em 2026-09-16).
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY()
	UComboBoxString* ModelComboBox = nullptr;

	UPROPERTY()
	UEditableTextBox* OutputDirBox = nullptr;

	UPROPERTY()
	UEditableTextBox* DatasetNameBox = nullptr;

	UPROPERTY()
	UComboBoxString* SunComboBox = nullptr;

	UPROPERTY()
	UEditableTextBox* GridStepXBox = nullptr;

	UPROPERTY()
	UEditableTextBox* GridStepYBox = nullptr;

	UPROPERTY()
	UEditableTextBox* GridStepZBox = nullptr;

	UPROPERTY()
	UCheckBox* RandomYawCheckBox = nullptr;

	UPROPERTY()
	UEditableTextBox* YawSamplesBox = nullptr;

	// So usado quando RandomYawCheckBox esta DESMARCADO -- lista de angulos
	// (graus, separados por virgula) varrida em CADA posicao do grid (ver
	// ADroneCaptureController::YawAnglesDeg).
	UPROPERTY()
	UEditableTextBox* ManualYawAnglesBox = nullptr;

	UFUNCTION()
	void OnStartClicked();

	// Gerador customizado de cada linha do dropdown -- o estilo default do
	// motor deixava o texto das opcoes escuro/pouco legivel contra o fundo
	// escuro do painel; isso forca branco sempre, sem depender de estilo.
	UFUNCTION()
	UWidget* GenerateComboEntryWidget(FString Item);
};
