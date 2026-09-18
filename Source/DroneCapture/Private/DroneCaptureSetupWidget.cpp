#include "DroneCaptureSetupWidget.h"

#include "DroneCaptureController.h"
#include "DroneCaptureTarget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"

namespace
{
	// Ordem tem que bater com as opcoes adicionadas em ModelComboBox.
	const TArray<EDroneModel> ModelOptionValues = {
		EDroneModel::DJIProMini, EDroneModel::DJI350RTK, EDroneModel::DJINeo,
		EDroneModel::DJIPhantom, EDroneModel::DJITello, EDroneModel::AirSimDefault,
	};

	// Ordem tem que bater com as opcoes adicionadas em SunComboBox.
	const TArray<ESunPreset> SunOptionValues = { ESunPreset::SolBaixo, ESunPreset::SolMedio, ESunPreset::SolAlto };

	// Tamanho de fonte UNICO pra tudo (exceto o titulo) -- padronizado.
	constexpr int32 BodyFontSize = 16;
	constexpr int32 TitleFontSize = 24;
}

UDroneCaptureSetupWidget::UDroneCaptureSetupWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// bIsFocusable so tem efeito setado aqui (construtor) -- setar depois
	// (ex: em NativeConstruct/NativeOnInitialized) e um no-op silencioso, e
	// SetWidgetToFocus() falha com "Attempting to focus Non-Focusable widget".
	bIsFocusable = true;
}

void UDroneCaptureSetupWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(FLinearColor(0.10f, 0.10f, 0.11f, 0.98f));
	Panel->SetPadding(FMargin(24.0f));
	UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(Panel);
	PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
	PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
	PanelSlot->SetAutoSize(true);
	PanelSlot->SetPosition(FVector2D::ZeroVector);

	UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box"));
	Panel->SetContent(Box);

	const FSlateFontInfo BodyFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), BodyFontSize);

	auto AddLabel = [&](const FString& Text, bool bTitle = false)
	{
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Label->SetText(FText::FromString(Text));
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		Label->SetFont(bTitle
			? FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), TitleFontSize)
			: BodyFont);
		UVerticalBoxSlot* Slot = Box->AddChildToVerticalBox(Label);
		Slot->SetPadding(FMargin(4.0f, bTitle ? 4.0f : 10.0f, 4.0f, 2.0f));
		return Label;
	};

	// Combo boxes de menu ficam com o texto das opcoes escuro por padrao
	// (estilo default do motor, pouco contraste contra o fundo escuro do
	// painel) -- GenerateComboEntryWidget forca cada linha a ter texto
	// branco, sem depender do estilo default.
	auto StyleComboBox = [&](UComboBoxString* Combo)
	{
		Combo->Font = BodyFont;
		Combo->ContentPadding = FMargin(8.0f, 6.0f);
		Combo->OnGenerateWidgetEvent.BindDynamic(this, &UDroneCaptureSetupWidget::GenerateComboEntryWidget);
	};

	// So a fonte (tamanho) nao muda a COR do texto digitado -- por padrao
	// sai num cinza claro lavado, quase ilegivel. Forca preto sobre fundo
	// branco explicitamente. ForegroundColor so vale em REPOUSO -- enquanto
	// a caixa esta com foco (digitando), quem manda e FocusedForegroundColor
	// (propriedade separada em FEditableTextBoxStyle); sem seta-la tambem,
	// o texto volta pro cinza claro padrao do estilo justo enquanto o
	// usuario esta preenchendo o campo.
	auto StyleEditBox = [&](UEditableTextBox* Box2)
	{
		Box2->WidgetStyle.SetFont(BodyFont);
		Box2->WidgetStyle.SetForegroundColor(FSlateColor(FLinearColor::Black));
		Box2->WidgetStyle.SetFocusedForegroundColor(FSlateColor(FLinearColor::Black));
		Box2->WidgetStyle.SetBackgroundColor(FSlateColor(FLinearColor::White));
	};

	auto AddEditBox = [&](const FString& DefaultValue) -> UEditableTextBox*
	{
		UEditableTextBox* Box2 = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		Box2->SetText(FText::FromString(DefaultValue));
		StyleEditBox(Box2);
		Box->AddChildToVerticalBox(Box2);
		return Box2;
	};

	AddLabel(TEXT("Configuracao da captura"), true);

	AddLabel(TEXT("Modelo de drone"));
	ModelComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass());
	ModelComboBox->AddOption(TEXT("DJI Pro Mini"));
	ModelComboBox->AddOption(TEXT("DJI 350RTK"));
	ModelComboBox->AddOption(TEXT("DJI Neo"));
	ModelComboBox->AddOption(TEXT("DJI Phantom"));
	ModelComboBox->AddOption(TEXT("DJI Tello"));
	ModelComboBox->AddOption(TEXT("AirSim Default"));
	ModelComboBox->SetSelectedIndex(0);
	StyleComboBox(ModelComboBox);
	Box->AddChildToVerticalBox(ModelComboBox);

	AddLabel(TEXT("Pasta base de saida"));
	OutputDirBox = AddEditBox(Controller ? Controller->OutputDir : TEXT("D:/TGv2"));

	AddLabel(TEXT("Nome do dataset"));
	DatasetNameBox = AddEditBox(Controller ? Controller->DatasetName : TEXT("dataset_sintetico"));

	AddLabel(TEXT("Passo do grid em metros (X / Y / Z)"));
	UHorizontalBox* StepRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Box->AddChildToVerticalBox(StepRow);
	auto AddStepBox = [&](float DefaultValue) -> UEditableTextBox*
	{
		UEditableTextBox* Box2 = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		Box2->SetText(FText::AsNumber(DefaultValue));
		StyleEditBox(Box2);
		UHorizontalBoxSlot* Slot = StepRow->AddChildToHorizontalBox(Box2);
		Slot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		Slot->SetSize(ESlateSizeRule::Fill);
		return Box2;
	};
	GridStepXBox = AddStepBox(Controller ? Controller->GridStepXM : 2.0f);
	GridStepYBox = AddStepBox(Controller ? Controller->GridStepYM : 2.0f);
	GridStepZBox = AddStepBox(Controller ? Controller->GridStepZM : 2.0f);

	UHorizontalBox* YawRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UVerticalBoxSlot* YawRowSlot = Box->AddChildToVerticalBox(YawRow);
	YawRowSlot->SetPadding(FMargin(4.0f, 10.0f, 4.0f, 2.0f));

	RandomYawCheckBox = WidgetTree->ConstructWidget<UCheckBox>(UCheckBox::StaticClass());
	RandomYawCheckBox->SetIsChecked(Controller ? Controller->bRandomYaw : false);
	YawRow->AddChildToHorizontalBox(RandomYawCheckBox);

	UTextBlock* YawLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	YawLabel->SetText(FText::FromString(TEXT("  Yaw aleatorio -- angulos por posicao:")));
	YawLabel->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	YawLabel->SetFont(BodyFont);
	UHorizontalBoxSlot* YawLabelSlot = YawRow->AddChildToHorizontalBox(YawLabel);
	YawLabelSlot->SetVerticalAlignment(VAlign_Center);

	YawSamplesBox = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
	YawSamplesBox->SetText(FText::AsNumber(Controller ? Controller->YawSamplesPerPoint : 1));
	StyleEditBox(YawSamplesBox);
	UHorizontalBoxSlot* YawSamplesSlot = YawRow->AddChildToHorizontalBox(YawSamplesBox);
	YawSamplesSlot->SetSize(ESlateSizeRule::Fill);

	// So usado se "Yaw aleatorio" acima estiver DESMARCADO -- varre TODOS
	// esses angulos (graus) em CADA posicao do grid, em vez de sortear.
	AddLabel(TEXT("Angulos manuais (graus, separados por virgula) -- so se \"Yaw aleatorio\" acima estiver desmarcado"));
	FString DefaultYawAnglesText = TEXT("0");
	if (Controller && Controller->YawAnglesDeg.Num() > 0)
	{
		TArray<FString> Parts;
		for (float Angle : Controller->YawAnglesDeg)
		{
			Parts.Add(FString::SanitizeFloat(Angle));
		}
		DefaultYawAnglesText = FString::Join(Parts, TEXT(", "));
	}
	ManualYawAnglesBox = AddEditBox(DefaultYawAnglesText);

	AddLabel(TEXT("Horario do dia"));
	SunComboBox = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass());
	SunComboBox->AddOption(TEXT("Sol baixo (amanhecer/entardecer)"));
	SunComboBox->AddOption(TEXT("Sol medio"));
	SunComboBox->AddOption(TEXT("Sol alto (meio-dia)"));
	SunComboBox->SetSelectedIndex(2);
	StyleComboBox(SunComboBox);
	Box->AddChildToVerticalBox(SunComboBox);

	UButton* StartButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	StartButton->OnClicked.AddDynamic(this, &UDroneCaptureSetupWidget::OnStartClicked);
	UTextBlock* ButtonLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	ButtonLabel->SetText(FText::FromString(TEXT("Iniciar Captura")));
	ButtonLabel->SetJustification(ETextJustify::Center);
	ButtonLabel->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), BodyFontSize));
	ButtonLabel->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	StartButton->AddChild(ButtonLabel);
	UVerticalBoxSlot* ButtonSlot = Box->AddChildToVerticalBox(StartButton);
	ButtonSlot->SetPadding(FMargin(4.0f, 20.0f, 4.0f, 4.0f));
	ButtonSlot->SetHorizontalAlignment(HAlign_Fill);

	UE_LOG(LogTemp, Log, TEXT("[DroneCapture] UDroneCaptureSetupWidget::NativeOnInitialized concluido (RootWidget=%s)."), WidgetTree->RootWidget ? TEXT("ok") : TEXT("NULO"));
}

UWidget* UDroneCaptureSetupWidget::GenerateComboEntryWidget(FString Item)
{
	// Este MESMO gerador e usado tanto pra cada linha da lista aberta
	// quanto pro "placeholder" (a caixa fechada mostrando a opcao
	// selecionada) -- a API do ComboBoxString nao da pra diferenciar os
	// dois contextos. Fix: cada item carrega o PROPRIO fundo claro (em vez
	// de confiar no fundo escuro/claro ao redor, que muda dependendo de
	// onde aparece), garantindo texto preto legivel nos dois lugares
	// sempre.
	UBorder* EntryBackground = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	EntryBackground->SetBrushColor(FLinearColor(0.92f, 0.92f, 0.93f, 1.0f));
	EntryBackground->SetPadding(FMargin(6.0f, 4.0f));

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Text->SetText(FText::FromString(Item));
	Text->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
	Text->SetFont(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), BodyFontSize));

	EntryBackground->SetContent(Text);
	return EntryBackground;
}

void UDroneCaptureSetupWidget::OnStartClicked()
{
	if (Controller)
	{
		const int32 ModelIndex = ModelComboBox ? ModelComboBox->GetSelectedIndex() : 0;
		if (ADroneCaptureTarget* Target = Cast<ADroneCaptureTarget>(Controller->Drone))
		{
			Target->SetDroneModel(ModelOptionValues.IsValidIndex(ModelIndex) ? ModelOptionValues[ModelIndex] : EDroneModel::DJIProMini);
		}

		if (OutputDirBox)
		{
			Controller->OutputDir = OutputDirBox->GetText().ToString();
		}
		if (DatasetNameBox)
		{
			Controller->DatasetName = DatasetNameBox->GetText().ToString();
		}

		if (GridStepXBox)
		{
			Controller->GridStepXM = FCString::Atof(*GridStepXBox->GetText().ToString());
		}
		if (GridStepYBox)
		{
			Controller->GridStepYM = FCString::Atof(*GridStepYBox->GetText().ToString());
		}
		if (GridStepZBox)
		{
			Controller->GridStepZM = FCString::Atof(*GridStepZBox->GetText().ToString());
		}

		if (RandomYawCheckBox)
		{
			Controller->bRandomYaw = RandomYawCheckBox->IsChecked();
		}
		if (YawSamplesBox)
		{
			Controller->YawSamplesPerPoint = FMath::Max(1, FCString::Atoi(*YawSamplesBox->GetText().ToString()));
		}
		if (ManualYawAnglesBox)
		{
			TArray<FString> Parts;
			ManualYawAnglesBox->GetText().ToString().ParseIntoArray(Parts, TEXT(","), true);
			TArray<float> ParsedAngles;
			for (FString& Part : Parts)
			{
				Part.TrimStartAndEndInline();
				if (!Part.IsEmpty())
				{
					ParsedAngles.Add(FCString::Atof(*Part));
				}
			}
			if (ParsedAngles.Num() > 0)
			{
				Controller->YawAnglesDeg = ParsedAngles;
			}
		}

		const int32 SunIndex = SunComboBox ? SunComboBox->GetSelectedIndex() : 2;
		Controller->SetSunPreset(SunOptionValues.IsValidIndex(SunIndex) ? SunOptionValues[SunIndex] : ESunPreset::SolAlto);
	}

	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->bShowMouseCursor = false;
		FInputModeGameOnly GameInput;
		PC->SetInputMode(GameInput);
	}

	RemoveFromParent();

	if (Controller)
	{
		Controller->StartCapture();
	}
}
