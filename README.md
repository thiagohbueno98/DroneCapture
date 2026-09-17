# DroneCapture

Plugin C++ para Unreal Engine 5.5 que gera um **dataset sintético de imagens
de drone** (visto por câmeras fixas espalhadas pelo mapa), pronto no formato
YOLO. Feito para um Trabalho de Graduação (detecção de drones), mas serve
pra qualquer projeto que precise de captura sintética automatizada:
posiciona um drone num grid 3D, checa oclusão/enquadramento em cada câmera,
calcula a bounding box e exporta imagem + label.

Roda inteiramente em **Play mode** (não em Editor Scripting) — isso evita
um vazamento de memória conhecido do `SceneCaptureComponent2D::CaptureScene()`
quando chamado fora do Play.

## Requisitos

- Unreal Engine **5.5**
- Visual Studio 2022 com o workload "Desenvolvimento para desktop com C++"
  (só é necessário se você for compilar o plugin — ver "Instalação")

## Instalação

1. Copie a pasta `DroneCapture/` inteira para dentro de `<SeuProjeto>/Plugins/`.
2. Abra o projeto no Editor (ou compile direto via linha de comando, ver
   abaixo) — o plugin já vem habilitado (`CanContainContent: true`), não
   precisa mexer em `.uplugin` nem em Content Browser.
3. Se o Editor pedir pra compilar módulos ausentes, aceite. Alternativa via
   linha de comando (mais confiável, principalmente se o Editor já estiver
   aberto com o DLL antigo travado):

   ```
   <caminho-do-Engine>\Engine\Build\BatchFiles\Build.bat UnrealEditor Win64 Development -Project=<caminho>\<SeuProjeto>.uproject
   ```

   Compile com o Editor **fechado**. Mudanças em **construtores** (ex:
   trocar o `ConstructorHelpers::FObjectFinder` de uma malha default) não
   são pegas pelo Live Coding — precisam dessa recompilação completa pra
   valer.

## Montando a cena

Sem rodar nenhum script Python — só arrastar 4 tipos de ator no nível e
preencher o Details panel:

| Ator | Quantidade | O que configurar |
|---|---|---|
| `ADroneCaptureTarget` | 1 | `DroneModel` (dropdown: DJI Pro Mini, DJI 350RTK, DJI Neo, DJI Phantom, DJI Tello, AirSim Default, ou `Custom` pra usar `BodyMesh`/`PropellerMesh` próprios) |
| `ADroneCaptureCamera` | N (uma por ponto de vista) | `CameraIndex` (1, 2, 3...), `RtWidth`/`RtHeight` (resolução final do dataset), `SupersampleFactor` (ver abaixo) |
| `ADroneCaptureGridVolume` | 1 | Escale a `Bounds` (BoxComponent) pra cobrir a região 3D onde o drone deve aparecer |
| `ADroneCaptureController` | 1 | Orquestra tudo — ver campos principais abaixo |

A descoberta de cena é automática (por classe); só é preciso preencher os
campos `Drone`/`Cameras`/`GridVolume` do Controller manualmente se você
tiver atores customizados fora dessas classes (nesse caso, use Actor Tags:
`"DroneAlvo"`, `"CaptureCam"`, `"VolumeGrid"`).

### Campos principais do `ADroneCaptureController`

- **Grid**: `GridStepXM/YM/ZM` (passo em metros por eixo), `bRandomYaw` +
  `YawSamplesPerPoint` (sorteia N ângulos por posição em vez de varrer uma
  lista fixa) ou `YawAnglesDeg` (lista fixa, usada se `bRandomYaw = false`).
- **Captura**: `WarmupCaptures` (frames reais de espera por pose antes de
  exportar — necessário pra Lumen/SSR/reflexos convergirem; aumente se
  reflexos de água/vidro ainda saírem "crus").
- **Qualidade**: `MinBboxAreaPx`, `EdgeMarginFraction`, `MinVisibleFraction`
  (fração mínima dos 8 cantos do drone que precisa estar desobstruída pra
  aceitar a amostra — bbox é sempre amodal, i.e. cobre a extensão total
  mesmo com oclusão parcial).
- **Saída**: `OutputDir` + `DatasetName` (caminho final =
  `OutputDir/DatasetName`), `Split` (`train`/`val`/...).
- **Debug**: `bSaveDiscardDebug` salva até `DiscardDebugLimitPerReason`
  imagens por motivo de descarte em `debug_descartados/<motivo>/`.
- **Hélices**: `bSpinPropellers`, `PropellerSpinDegPerSec` — giram
  nativamente durante o Play (Yaw é o eixo correto pros 6 modelos
  bundlados; hélices diagonais opostas giram no mesmo sentido, as do
  mesmo lado em sentidos opostos, como um quadricóptero real).
- **Menu**: `bShowSetupMenuOnBeginPlay` (default `true`) mostra um menu ao
  apertar Play; desligue pra pular direto pro fluxo antigo
  (`bAutoStartOnBeginPlay`), útil em automação/teste via script.

### Supersampling (anti-aliasing / redução de ruído)

Cada `ADroneCaptureCamera` tem um campo `SupersampleFactor` (padrão `2`,
1–4). A câmera renderiza numa resolução `Factor` vezes maior que
`RtWidth`/`RtHeight`, e o Controller reduz pra resolução final fazendo uma
média real dos pixels em espaço linear (box filter) antes de salvar o PNG
— mesma ideia de Super-Sampling Anti-Aliasing (SSAA), deixa a imagem bem
mais "limpa"/natural que exportar direto na resolução final. `Factor = 1`
desliga (comportamento antigo, sem custo extra). Fator maior = captura
mais lenta (há um readback de GPU por imagem exportada).

## Rodando

Aperte **Play**. Com o menu de setup ligado (padrão), aparece um formulário
pedindo: modelo de drone, pasta/nome do dataset, passo do grid (X/Y/Z), yaw
aleatório (liga/desliga + quantos ângulos por posição), e horário do dia
(3 presets de ângulo de sol). Clique **Iniciar Captura**.

A captura roda 1 pose por Tick real (grid inteiro × yaws × câmeras) —
acompanhe o progresso pelo Output Log. Pra parar antes do fim, chame
`StopCapture()` (BlueprintCallable, também exposto via Details/console).

## Formato de saída

```
<OutputDir>/<DatasetName>/
├── images/<Split>/CaptureCamN_<pose_index>.png
├── labels/<Split>/CaptureCamN_<pose_index>.txt   # formato YOLO: "0 xc yc w h" (normalizado 0-1)
├── data.yaml                                      # escrito ao final da rodada
└── debug_descartados/<motivo>/...                 # só se bSaveDiscardDebug = true
```

## Estrutura do plugin

- `ADroneCaptureController` — orquestra a rodada inteira (grid, oclusão,
  bbox, export, giro de hélice, menu de setup). Ponto de entrada de tudo.
- `ADroneCaptureTarget` — o drone (corpo + 4 hélices), com 6 modelos
  bundlados em `Content/Meshes/` (malha + materiais + texturas próprios,
  sem depender de nenhum asset fora do plugin).
- `ADroneCaptureCamera` — câmera de captura pronta pra uso (cria o próprio
  Render Target em runtime, já com Lumen/exposição/pós-processo
  configurados).
- `ADroneCaptureGridVolume` — só marca a região 3D do grid (BoxComponent
  invisível, sem lógica própria).
- `UDroneCaptureSetupWidget` — menu de configuração mostrado ao apertar
  Play (UMG montado 100% em C++, sem Widget Blueprint).

## Avisos

- **Malhas de drone bundladas** (`Content/Meshes/`) vieram de asset packs
  de terceiros (AirSim/SimBlank) reempacotados pra dentro do plugin —
  confira a licença original de cada asset antes de redistribuir/usar
  comercialmente.
- Testado apenas em Windows com UE 5.5. Não testado em builds
  packaged/standalone (só Editor + Play-in-Editor).
