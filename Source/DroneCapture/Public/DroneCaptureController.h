#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "GameFramework/Actor.h"
#include "DroneCaptureTarget.h"
#include "DroneCaptureController.generated.h"

class ACameraActor;

class USceneCaptureComponent2D;
class UDroneCaptureSetupWidget;
class UTextureRenderTarget2D;

UENUM(BlueprintType)
enum class ESunPreset : uint8
{
	SolBaixo,  // -30 graus -- amanhecer/entardecer (validado em contexto.md)
	SolMedio,  // -55 graus -- interpolado, nunca testado de verdade
	SolAlto,   // -85 graus -- meio-dia (validado em contexto.md)
};

UENUM(BlueprintType)
enum class EPoseCheckStatus : uint8
{
	Ok = 0,
	Oclusao = 1,
	ForaDoCampoDeVisao = 2,
	BboxPequena = 3,
	BboxNaBorda = 4,
	BboxMuitoGrande = 5,
	PoucoVisivel = 6,
};

// EPropellerSpinAxis agora mora em DroneCaptureTarget.h (e por-modelo, nao
// mais fixo aqui -- ver comentario la).

USTRUCT()
struct FPoseCheckResult
{
	GENERATED_BODY()

	EPoseCheckStatus Status = EPoseCheckStatus::Oclusao;
	FVector2D BboxMin = FVector2D::ZeroVector;
	FVector2D BboxMax = FVector2D::ZeroVector;
	int32 RtWidth = 0;
	int32 RtHeight = 0;
	int32 VisiblePixelCount = 0;
};

/**
 * Porta nativa (C++) do pipeline de captura de dataset sintetico de drones,
 * antes implementado como Level Blueprint + ponte Python (ver
 * D:\TGv2\contexto.md, Pivo 3/4). Roda em Play mode -- CaptureScene() chamado
 * daqui nao tem o vazamento de memoria conhecido da engine que acontece
 * quando chamado via Python Editor Scripting fora do Play.
 *
 * Uma pose completa (mover o drone + checar todas as cameras) e processada
 * por Tick, mesmo desenho ja validado no Level Blueprint do Pivo 3.
 */
UCLASS(ClassGroup = (DroneCapture))
class DRONECAPTURE_API ADroneCaptureController : public AActor
{
	GENERATED_BODY()

public:
	ADroneCaptureController();

	// ------------------------------------------------------------------
	// Referencias de cena. Deixe em branco para descoberta automatica por
	// Actor Tag (mesmo padrao ja criado por setup_cena.py): Drone ->
	// "DroneAlvo", Cameras -> "CaptureCam", GridVolume -> "VolumeGrid".
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Cena")
	AActor* Drone = nullptr;

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Cena")
	TArray<AActor*> Cameras;

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Cena")
	AActor* GridVolume = nullptr;

	// ------------------------------------------------------------------
	// Configuracao do grid / captura (equivalente a capture_core.py)
	// ------------------------------------------------------------------

	// Usados so se bRandomYaw = false (modo "manual" -- varre TODOS esses
	// angulos em CADA posicao do grid). Default com 1 angulo so -- o modo
	// manual e pra quem quer escolher angulos especificos (editavel no menu
	// de setup, ver DroneCaptureSetupWidget), nao um sweep pre-definido.
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Grid", meta = (EditCondition = "!bRandomYaw"))
	TArray<float> YawAnglesDeg = { 0.f };

	// Se ligado (DEFAULT), ignora YawAnglesDeg -- sorteia YawSamplesPerPoint
	// angulos aleatorios (0-360) por posicao, em vez de varrer uma lista
	// fixa manual. Usar YawSamplesPerPoint=1 pra so 1 pose por posicao.
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Grid")
	bool bRandomYaw = true;

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Grid", meta = (EditCondition = "bRandomYaw", ClampMin = "1"))
	int32 YawSamplesPerPoint = 1;

	// Passo do grid por EIXO (metros) -- ajustavel separado pra ir mais
	// rapido num eixo que no outro (ex: aumentar o passo em Y pra "andar"
	// mais rapido pro fundo da cena).
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Grid")
	float GridStepXM = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Grid")
	float GridStepYM = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Grid")
	float GridStepZM = 2.0f;

	// Numero de frames DE VERDADE (1 por Tick real, nao um loop dentro do
	// mesmo Tick) que a camera espera parada numa pose antes de exportar --
	// necessario pra Lumen/SSR/TAA acumularem historico temporal (reflexo
	// de agua, GI etc.) de verdade. Chamar CaptureScene() varias vezes
	// dentro do MESMO Tick nao adianta pra isso: o "frame" usado pelo
	// acumulo temporal so muda entre Ticks reais do jogo.
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Captura")
	int32 WarmupCaptures = 5;

	// Numero MINIMO de pixels da mascara que realmente pertencem ao drone
	// (contagem de pixel de verdade, nao a area do retangulo da bbox).
	// Achado rodando o dataset de verdade: um drone quase todo ocluido, com
	// so a ponta de 1-2 helices espiando por tras de um obstaculo em cantos
	// bem separados, gera uma bbox (retangulo que envolve os 2 fragmentos)
	// com area consideravel mesmo tendo pouquissimo drone de fato visivel --
	// medir pixel real em vez da area do retangulo pega esse caso.
	//
	// Default baixo de proposito: com 200, o dataset ficava sem drone distante
	// (95% das caixas com mais de 39 px de lado) e o detector perdia quase todo
	// drone real menor que 32 px. Medido com o DJI Mini a FOV 90 em 1920x1080:
	// caixa de ~1600/distancia(m) px de largura, e 15 px de mascara equivalem a
	// uma caixa de ~15x5 px (~110 m). O caso do drone quase todo ocluido e pego
	// por MinVisibleFraction.
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Qualidade", meta = (ClampMin = "1"))
	int32 MinDronePixelCount = 15;

	// Abaixo dessa contagem de pixels visiveis nao da pra saber, so pela
	// mascara, se o drone esta longe (amostra boa) ou quase todo escondido
	// (amostra ruim). Nesse caso a mascara e capturada de novo com SO o drone
	// na cena (silhueta inteira, sem nada na frente) e a pose so e aceita se
	// pelo menos MinVisibleFraction da silhueta estiver visivel. Acima dessa
	// contagem a pose e aceita direto, como sempre foi. MinVisibleFraction = 0
	// desliga a checagem.
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Qualidade", meta = (ClampMin = "0"))
	int32 OcclusionCheckPixelCount = 200;

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Qualidade", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinVisibleFraction = 0.5f;

	// Margem (fracao da largura/altura) que a bbox precisa manter em relacao
	// a borda da imagem pra NAO ser descartada. Default 0 -- desligado de
	// proposito: o YOLO vai detectar drone em tempo real, e um drone
	// entrando/saindo de quadro (bbox cortada na borda) e um caso real que
	// precisa aparecer no dataset, nao um erro. A bbox ja vem dos pixels
	// reais da mascara (sempre dentro dos limites da imagem por construcao)
	// -- com margem 0 essa checagem nunca dispara, entao toda pose com pelo
	// menos MinDronePixelCount de drone visivel na tela passa. Aumente pra
	// um valor > 0 se quiser voltar a descartar poses cortadas (ex: dataset
	// que so precisa de drone inteiro em quadro).
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Qualidade")
	float EdgeMarginFraction = 0.0f;

	// Descarta a pose se a bbox da mascara cobrir mais que essa fracao da
	// area total da imagem. Nao e um limite realista pro drone (que nunca
	// enche o quadro inteiro nas distancias usadas aqui) -- e uma trava de
	// seguranca contra o material M_DroneMask cair no fallback (mostra a
	// cena RGB inteira em vez de preto/branco) num frame isolado, o que faz
	// ComputeMaskBbox enxergar "drone" em toda a imagem. Achado rodando o
	// dataset de verdade pela 1a vez: pose_index=0 saiu com bbox=imagem
	// inteira porque o shader do material ainda estava compilando na
	// PRIMEIRA CaptureScene() da mascara (ver aquecimento em
	// ResolveSceneReferences() e contexto.md).
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Qualidade")
	float MaxBboxAreaFraction = 0.9f;

	// ------------------------------------------------------------------
	// Mascara de segmentacao -- oclusao e bbox saem dos PIXELS DE VERDADE
	// de uma segunda captura onde so o drone aparece (branco), em vez de
	// aproximar por raycast+projecao geometrica (achado 2026-09-17: colisao
	// simplificada de mapas grandes tipo o CitySample deixava o raycast
	// antigo passar por baixo de beirais/telhados sem colisao complexa,
	// gerando bbox falso-positiva). Tambem elimina a bbox inflada em yaws
	// diagonais do AABB do ator antigo -- a bbox agora e sempre exatamente
	// do tamanho da silhueta visivel.
	//
	// IMPORTANTE: CustomStencil sozinho (so no drone) NAO respeita oclusao
	// de objetos comuns da cena -- CustomDepth e um buffer separado do
	// buffer normal, so objetos TAMBEM marcados participam do Z-test dele.
	// O material M_DroneMask por isso compara SceneTexture(CustomDepth) do
	// drone contra SceneTexture(SceneDepth) da cena inteira (que reflete
	// QUALQUER objeto opaco comum) -- so conta como "drone visivel" quando
	// o stencil bate E o CustomDepth nao esta atras do que o SceneDepth
	// enxerga como mais proximo naquele pixel. Tentativa alternativa de
	// marcar TODO O RESTO da cena com CustomDepth tambem (evitando essa
	// comparacao entre 2 buffers) foi abandonada: quebra em niveis
	// Nanite-pesados como o CityPark/CitySample (malhas Nanite forcam
	// fallback pra renderizacao nao-Nanite quando marcadas CustomDepth em
	// massa, mesmo com r.Nanite.CustomDepth=1 -- 0 amostras validas na
	// captura de teste). Ver contexto.md.
	// ------------------------------------------------------------------

	// Valor de CustomDepth Stencil (0-255) usado SO nos componentes do
	// drone. O material M_DroneMask2 so aceita EXATAMENTE 250 (valor fixo no
	// grafo, ver ue_python/criar_material_mascara_v2.py) -- o antigo aceitava
	// qualquer stencil >= limiar, e partes dos carros do CitySample (que tambem
	// escrevem CustomStencil) entravam na mascara e esticavam a bbox. Se mudar
	// este valor, mude o material junto. Ligue bSaveMaskDebug pra conferir
	// visualmente se a mascara sai limpa (so o drone, resto preto).
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Mascara")
	int32 MaskStencilValue = 250;

	// Intensidade minima (0-255) de qualquer canal RGB da mascara pra
	// contar como pixel de drone -- filtra ruido de antialiasing na borda.
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Mascara")
	int32 MaskPixelThreshold = 10;

	// Salva a imagem crua da mascara (debug_mascara/) junto de cada pose
	// processada, pra conferencia visual.
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Mascara")
	bool bSaveMaskDebug = false;

	// ------------------------------------------------------------------
	// Negativos -- quando o drone nao aparece pra uma camera (nenhum pixel
	// da mascara detectado -- oclusao total ou fora do campo de visao, a
	// mascara nao distingue os dois casos), em vez de so descartar a pose,
	// sorteia se salva ela como amostra NEGATIVA (imagem + label VAZIO, convencao YOLO
	// pra "sem objeto"). Importante pro detector aprender a nao alucinar
	// drone em fundo vazio -- mas nao salva 100% dos casos de proposito: a
	// fracao de poses sem drone visivel tende a ser BEM maior que a de
	// poses com drone, salvar todas inundaria o dataset com fundo repetido.
	// ------------------------------------------------------------------

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Negativos")
	bool bSaveNegativeSamples = true;

	// Chance (0-1) de UMA pose sem drone visivel virar amostra negativa
	// exportada. Ex: 0.05 = ~5% dessas poses viram negativo, o resto so e
	// descartado (ou vai pro debug_descartados/ se bSaveDiscardDebug=true).
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Negativos", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NegativeSampleChance = 0.05f;

	// Pasta BASE -- o caminho de verdade usado na exportacao e
	// GetEffectiveOutputDir() = OutputDir/DatasetName (ver abaixo). Separar
	// os dois deixa facil rodar varios datasets sem retypar o caminho
	// inteiro toda vez (ex: no menu de setup, ver DroneCaptureSetupWidget).
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Saida")
	FString OutputDir = TEXT("D:/TGv2");

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Saida")
	FString DatasetName = TEXT("dataset_sintetico");

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Saida")
	FString Split = TEXT("train");

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Debug")
	bool bSaveDiscardDebug = false;

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Debug")
	int32 DiscardDebugLimitPerReason = 20;

	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	bool bAutoStartOnBeginPlay = true;

	// Aplicado no BeginPlay (nao no StartCapture) -- ver comentario la.
	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	bool bApplyQualitySettingsOnStart = true;

	// Desliga a renderizacao do mundo na janela do jogo enquanto a captura
	// roda (so o HUD de progresso continua aparecendo, em fundo preto).
	// DESLIGADO por padrao: testado no Park (2026-10-09), a imagem salva
	// PIORA -- o lago perde o reflexo e o ceu perde as nuvens, que pelo visto
	// so sao atualizados quando a janela principal renderiza. O ganho de
	// tempo tambem foi pequeno.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Capture")
	bool bDisableViewportRenderingWhileCapturing = false;

	// Desde o BeginPlay ate o fim da rodada, poe a vista do jogador (a janela
	// do jogo) na propria camera de captura e ignora mouse/movimento. Reflexo planar (APlanarReflection -- lago e fonte do
	// Park) e renderizado so pra vista PRINCIPAL e a captura reaproveita o
	// resultado: com o jogador em outro lugar ou olhando pra outro lado o
	// reflexo saia errado ou sumia, mudando de uma rodada pra outra e ate no
	// meio da rodada (medido em 2026-10-09, testes sem_mexer x mexendo).
	// Com mais de uma camera elas passam a ser capturadas uma por vez, cada
	// uma com o proprio aquecimento. Nao combina com
	// bDisableViewportRenderingWhileCapturing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Capture")
	bool bMatchPlayerViewToCaptureCamera = true;

	// Folga somada ao FOV da captura na vista do jogador (ver acima).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Capture", meta = (ClampMin = "0.0", ClampMax = "40.0", EditCondition = "bMatchPlayerViewToCaptureCamera"))
	float PlayerViewExtraFovDeg = 10.0f;

	// ------------------------------------------------------------------
	// Espera de carregamento do mapa -- achado rodando city1 (2026-09-23):
	// as primeiras poses saiam com carros/trafego ainda nao renderizados
	// (populam aos poucos depois do BeginPlay, sistema de spawn da propria
	// cena, fora do controle deste plugin), contaminando o inicio do
	// dataset com cenas incompletas. StartCapture() agora so comeca a
	// mover o drone/capturar depois dessa espera (tempo minimo fixo +,
	// opcionalmente, streaming de nivel completo) -- ver Tick(),
	// bWaitingForLevelLoad.
	// ------------------------------------------------------------------

	// Tempo minimo (segundos, tempo real apos StartCapture) esperado antes
	// da 1a pose -- da tempo de sistemas de spawn (trafego, pedestres etc.)
	// popularem a cena. Ajuste empiricamente por mapa (mapas mais pesados
	// podem precisar de mais). 0 desliga a espera fixa (so streaming, se
	// bWaitForLevelStreamingComplete estiver ligado).
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Carregamento", meta = (ClampMin = "0.0"))
	float MapLoadWaitSeconds = 15.0f;

	// Alem do tempo fixo acima, espera todo ULevelStreaming do mundo sair
	// dos estados "carregando" (relevante em mapas com World Partition,
	// que streamam celulas conforme o Pawn observador se move). Nao cobre
	// sistemas de spawn de trafego/pedestres proprios da cena (esses so
	// tem o tempo fixo acima como mitigacao).
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Carregamento")
	bool bWaitForLevelStreamingComplete = true;

	// Se ligado, BeginPlay mostra um menu (modelo de drone, pasta/nome do
	// dataset, horario do dia) antes de comecar -- bAutoStartOnBeginPlay
	// so tem efeito se este estiver DESLIGADO (fluxo antigo, sem menu, pra
	// automacao/teste via script Python).
	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	bool bShowSetupMenuOnBeginPlay = true;

	// Widget usado pro menu -- default e a classe C++ base (funciona sem
	// configurar nada); troque por uma Widget Blueprint filha se quiser
	// um visual proprio.
	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	TSubclassOf<UDroneCaptureSetupWidget> SetupWidgetClass;

	// ------------------------------------------------------------------
	// Helices -- girar_helices.py rodava no mundo do EDITOR, entao nao
	// afetava o drone duplicado do mundo de Play (PIE) usado de verdade na
	// captura. Rotacao nativa aqui opera direto no "Drone" ja resolvido
	// (referencia certa do mundo de Play).
	// ------------------------------------------------------------------

	// Trava todas as malhas do drone no LOD0. Sem isso o motor troca pela
	// malha simplificada quando o drone fica pequeno na tela e a silhueta
	// perde bracos/helices -- justo nos drones distantes. A trava vale pra
	// todas as cameras (o LOD e escolhido por componente). Malha Nanite
	// ignora (tem LOD proprio por cluster). DESLIGADO por padrao (em teste
	// A/B desde 2026-10-09).
	UPROPERTY(EditAnywhere, Category = "Drone Capture")
	bool bForceDroneLod0 = false;

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Helices")
	bool bSpinPropellers = true;

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Helices")
	float PropellerSpinDegPerSec = 1440.0f;

	// Casamento por NOME do componente (substring, case-insensitive) OU
	// pelo nome da malha estatica -- mesmo criterio duplo do
	// girar_helices.py original.
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Helices")
	FString PropellerNameContains = TEXT("Helice");

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Helices")
	FString PropellerMeshName = TEXT("Propeller");

	// Fallback usado SO se "Drone" nao for um ADroneCaptureTarget (ator
	// customizado sem esse campo) -- quando for um ADroneCaptureTarget
	// normal, SpinPropellers() usa o PropellerSpinAxis DELE (por-modelo,
	// ver DroneCaptureTarget.h), nao este aqui.
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Helices")
	EPropellerSpinAxis PropellerSpinAxis = EPropellerSpinAxis::Yaw;

	// Blur de rotacao da helice na imagem RGB -- SceneCaptureComponent2D
	// NAO suporta motion blur nativo do motor (limitacao conhecida da
	// engine: o passe de velocidade que o motion blur depende nao roda no
	// caminho de scene capture, confirmado testando MotionBlurAmount +
	// ShowFlags.SetMotionBlur(true) sem nenhum efeito). Simulado aqui na
	// unha: captura PropellerBlurSamples sub-frames da helice varrendo
	// PropellerBlurSweepDeg graus (terminando exatamente no angulo real
	// final, pra bater com o instante usado pela mascara) e faz a MEDIA em
	// espaco linear -- mesma ideia do supersampling espacial
	// (ExportCaptureToPng), so que no tempo em vez de no espaco. 1 ou
	// menos = desliga (comportamento antigo, helice sempre nitida).
	// Custo: PropellerBlurSamples CaptureScene()+ReadPixels() extras POR
	// CAMERA a cada pose exportada.
	UPROPERTY(EditAnywhere, Category = "Drone Capture|Helices", meta = (ClampMin = "1", ClampMax = "16"))
	int32 PropellerBlurSamples = 6;

	UPROPERTY(EditAnywhere, Category = "Drone Capture|Helices", meta = (ClampMin = "0.0", ClampMax = "90.0", EditCondition = "PropellerBlurSamples > 1"))
	float PropellerBlurSweepDeg = 20.0f;

	// ------------------------------------------------------------------
	// Controle (chamavel de Blueprint se quiser disparar na mao)
	// ------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Drone Capture")
	void StartCapture();

	UFUNCTION(BlueprintCallable, Category = "Drone Capture")
	void StopCapture();

	UFUNCTION(BlueprintCallable, Category = "Drone Capture")
	bool IsRunning() const { return bIsRunning; }

	UFUNCTION(BlueprintCallable, Category = "Drone Capture")
	float GetProgress() const;

	// OutputDir/DatasetName combinados -- caminho de verdade usado em
	// export/data.yaml.
	UFUNCTION(BlueprintCallable, Category = "Drone Capture")
	FString GetEffectiveOutputDir() const;

	// Acha a primeira DirectionalLight do nivel e ajusta o Pitch (mantendo
	// Yaw/Roll atuais). Chamado pelo menu de setup.
	UFUNCTION(BlueprintCallable, Category = "Drone Capture")
	void SetSunPreset(ESunPreset Preset);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// Estado da rodada
	bool bIsRunning = false;
	int32 PoseIndex = 0;
	TArray<FVector> GridPoints;
	TArray<USceneCaptureComponent2D*> CameraComponents;
	TMap<FString, int32> DiscardCountByReason;

	// Estado da espera de carregamento do mapa (ver MapLoadWaitSeconds no
	// header) -- true entre o StartCapture() e a 1a pose de verdade.
	bool bWaitingForLevelLoad = false;
	float LevelLoadElapsedSeconds = 0.0f;

	// Timestamp (FPlatformTime::Seconds()) de quando a 1a pose de verdade
	// comecou -- usado pra estimar tempo restante no HUD de progresso (ver
	// ShowProgressOnScreen).
	double CaptureStartTimeSeconds = 0.0;

	// Mostra num HUD simples (GEngine::AddOnScreenDebugMessage, mesma
	// "linha" reaproveitada via chave fixa) quantas poses ja foram
	// processadas e uma estimativa de tempo restante -- so uma nocao
	// aproximada de progresso durante uma rodada longa, nao precisao.
	void ShowProgressOnScreen(int32 Total) const;

	bool IsLevelStreamingComplete() const;

	// Pixels RGB ja com o blur de helice aplicado (media de varios sub-
	// frames, ver CapturePropellerMotionBlur), preenchido 1x por pose antes
	// do loop de export -- ExportCaptureToPng usa isso em vez de reler a
	// GPU quando presente (a textura em si so tem o ULTIMO sub-frame,
	// nitido). Fica vazio se PropellerBlurSamples <= 1.
	TMap<USceneCaptureComponent2D*, TArray<FColor>> PendingBlurredRgbPixels;

	// Estado da fase de "aquecimento" (ver comentario em WarmupCaptures) da
	// pose atual. -1 = pose ainda nao comecou (precisa mover o drone); >0 =
	// ainda faltam N Ticks reais de CaptureScene() antes de poder exportar;
	// 0 = aquecimento concluido -- captura a mascara e exporta neste Tick.
	int32 WarmupFramesLeft = -1;
	float PendingYawDeg = 0.0f;

	// Estado da rotacao das helices -- base capturada 1x por componente
	// (por nome, nao por ponteiro -- seguro mesmo se Drone for um ator
	// Blueprint que reconstroi componentes, ver gotcha 1/17 em contexto.md)
	// e um angulo acumulado desde o BeginPlay.
	TMap<FName, FRotator> PropellerBaseRotations;
	float PropellerSpinAngleDeg = 0.0f;

	void SpinPropellers(float DeltaSeconds);

	// Aplica um angulo de giro EXPLICITO nas helices (extraido de
	// SpinPropellers pra reuso em CapturePropellerMotionBlur, que precisa
	// varrer varios angulos sub-frame sem mexer no PropellerSpinAngleDeg
	// "de verdade" acumulado por Tick).
	void SetPropellerSpinAngle(float AngleDeg);

	// Simula motion blur de helice (ver PropellerBlurSamples no header) --
	// preenche PendingBlurredRgbPixels por camera, restaura o angulo real
	// da helice no final (a mascara, capturada logo depois, precisa bater
	// com esse angulo).
	void CapturePropellerMotionBlur(USceneCaptureComponent2D* RgbComp);
	void ShowSetupMenu();
	int32 GetYawCount() const { return bRandomYaw ? FMath::Max(1, YawSamplesPerPoint) : YawAnglesDeg.Num(); }

	// --------- equivalentes 1:1 de capture_core.py ---------

	void ResolveSceneReferences();
	void BuildGridPoints();
	void ApplyQualitySettings();

	// CVars sao globais do processo do editor (nao do mundo do PIE): o que
	// ApplyQualitySettings/ConfigureDroneMask mudam continuaria valendo
	// depois do Stop. SetCVarRemembering guarda o valor ORIGINAL (so na 1a
	// mudanca de cada CVar) e RestoreCVars volta tudo no EndPlay.
	void SetCVarRemembering(const TCHAR* Name, const FString& Value);
	void RestoreCVars();

	// Ver bDisableViewportRenderingWhileCapturing. So religa o que foi
	// desligado por aqui.
	void SetGameViewportWorldRendering(bool bEnabled);
	bool bViewportRenderingDisabledByCapture = false;

	// Imagens entregues pra compressao/gravacao em background e ainda nao
	// terminadas (ver ExportCaptureToPng). Compartilhado com as tarefas, que
	// podem terminar depois do ator.
	TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> PendingImageWrites = MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	void WaitForPendingImageWrites() const;

	// Ver bMatchPlayerViewToCaptureCamera.
	void SetPlayerViewToCamera(int32 CameraIdx);
	void RestorePlayerView();
	int32 GetWarmupFrameCount() const;
	int32 ActiveCameraIndex = 0;
	int32 PlayerViewCameraIndex = 0;
	bool bPlayerViewMovedByCapture = false;

	// Camera auxiliar que recebe a vista do jogador (ver SetPlayerViewToCamera).
	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> PlayerViewCamera = nullptr;

	// Tempo acumulado por etapa da rodada -- gravado em <dataset>/tempos.txt
	// e no log quando a rodada termina (WriteTimings).
	struct FCaptureTimings
	{
		double WarmupSec = 0.0;
		double BlurRenderSec = 0.0;
		double BlurReadbackSec = 0.0;
		double BlurCpuSec = 0.0;
		double MaskSec = 0.0;
		double ExportSec = 0.0;
		int32 Poses = 0;
		int32 Saved = 0;
	};
	FCaptureTimings Timings;
	double PoseStartSeconds = 0.0;
	void WriteTimings() const;
	TMap<FString, FString> OriginalCVarValues;
	void WriteDataYaml() const;

	// Marca os componentes do drone com CustomStencil=MaskStencilValue --
	// chamado 1x quando "Drone" e resolvido (ver ResolveSceneReferences()).
	void ConfigureDroneMask();

	// Le os pixels da mascara de volta da GPU e acha o retangulo (em
	// pixels) que envolve todos os pixels que batem com MaskPixelThreshold,
	// alem da CONTAGEM real desses pixels (OutVisiblePixelCount -- usada por
	// BboxQualityReason em vez da area do retangulo, ver MinDronePixelCount).
	bool ComputeMaskBbox(UTextureRenderTarget2D* MaskTarget, FVector2D& OutMin, FVector2D& OutMax, int32& OutVisiblePixelCount) const;

	EPoseCheckStatus BboxQualityReason(const FVector2D& Min, const FVector2D& Max, int32 Width, int32 Height, int32 VisiblePixelCount) const;

	// Substitui a antiga checagem geometrica (raycast + projecao de
	// cantos) -- captura a mascara, le os pixels, e decide oclusao/bbox a
	// partir do que realmente apareceu na imagem.
	FPoseCheckResult CheckPoseFromMask(USceneCaptureComponent2D* MaskComp) const;

	// Captura a mascara de novo com so o drone na cena e devolve quantos
	// pixels a silhueta inteira ocupa (ver OcclusionCheckPixelCount).
	// Sobrescreve o TextureTarget da mascara.
	int32 CountUnoccludedDronePixels(USceneCaptureComponent2D* MaskComp) const;

	// Salva a imagem crua da mascara pra conferencia visual (bSaveMaskDebug).
	void ExportMaskDebug(USceneCaptureComponent2D* MaskComp, const FString& CamLabel, const FString& SampleKey) const;

	void ExportSample(const FString& CamLabel, const FString& SampleKey, USceneCaptureComponent2D* RgbComp, const FVector2D& BboxMin, const FVector2D& BboxMax, int32 RtWidth, int32 RtHeight, int32 SupersampleFactor) const;

	// Salva imagem + label VAZIO (convencao YOLO pra "sem objeto") -- usado
	// pra amostras negativas sorteadas (ver bSaveNegativeSamples).
	void ExportNegativeSample(const FString& CamLabel, const FString& SampleKey, USceneCaptureComponent2D* RgbComp, int32 SupersampleFactor) const;

	void ExportDiscardDebug(const FString& Reason, const FString& CamLabel, const FString& SampleKey, USceneCaptureComponent2D* RgbComp, const FString& InfoText, int32 SupersampleFactor) const;

	// Exporta RgbComp->TextureTarget pra PNG em OutDir/FileName. Se
	// SupersampleFactor > 1, o TextureTarget foi renderizado maior de
	// proposito (ver ADroneCaptureCamera::SupersampleFactor) -- reduz aqui
	// via box filter (media dos texels em espaco linear) antes de salvar,
	// em vez de so redimensionar a imagem grande (isso e o que de fato
	// reduz ruido/aliasing).
	void ExportCaptureToPng(USceneCaptureComponent2D* RgbComp, int32 SupersampleFactor, const FString& OutDir, const FString& FileName) const;

	static FString StatusToReasonString(EPoseCheckStatus Status);
};
