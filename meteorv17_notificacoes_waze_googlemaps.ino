#include <Arduino.h>
#include <TFT_eSPI.h>
#include <NimBLEDevice.h>
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include "meteor_bluetooth.h"


// ============================================================
// MOTONAV
// ============================================================
//
// Tenstar T-Display
// ESP32-D0WD
//
// ESP32 Arduino Core 3.3.11
// NimBLE-Arduino 2.5.1
//
// ============================================================


// ============================================================
// TFT
// ============================================================

TFT_eSPI tft = TFT_eSPI();


// ============================================================
// DIMENSÕES
// ============================================================

#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 135

// Botão 1 do T-Display
#define BUTTON_1_PIN 0


// ============================================================
// ANCS UUIDs
// ============================================================

static NimBLEUUID ANCS_SERVICE_UUID(
    "7905F431-B5CE-4E99-A40F-4B1E122D00D0"
);

static NimBLEUUID NOTIFICATION_SOURCE_UUID(
    "9FBF120D-6301-42D9-8C58-25E699A21DBD"
);

static NimBLEUUID CONTROL_POINT_UUID(
    "69D1D8F3-45E1-49A8-9821-9BBDFDAAD9D9"
);

static NimBLEUUID DATA_SOURCE_UUID(
    "22EAC6E9-24D6-4BB5-BE44-B36ACE7C7BFB"
);

// ============================================================
// APPLE MEDIA SERVICE (AMS)
// ============================================================

static NimBLEUUID AMS_SERVICE_UUID(
    "89D3502B-0F36-433A-8EF4-C502AD55F8DC"
);

static NimBLEUUID AMS_ENTITY_UPDATE_UUID(
    "2F7CABCE-808D-411F-9A0C-BB92BA96C102"
);

static NimBLEUUID AMS_ENTITY_ATTRIBUTE_UUID(
    "C6B2F38C-23AB-46D8-A6AB-A3A870BBD5D7"
);

// ============================================================
// SERVICO BLE LOCAL - METEOR 350CC
// ============================================================
static NimBLEUUID METEOR_SERVICE_UUID(
    "7A6D0001-8B2C-4F31-9C10-350CC0000001"
);

static NimBLEUUID METEOR_STATUS_UUID(
    "7A6D0002-8B2C-4F31-9C10-350CC0000001"
);

NimBLEService* pMeteorService = nullptr;
NimBLECharacteristic* pMeteorStatusCharacteristic = nullptr;;


// ============================================================
// GIFS DE IDLE
// ============================================================
// Os GIFs ficam na particao LittleFS (pasta data/ do projeto).
// A tela usa um GIF aleatorio quando nao ha notificacao nem midia.
// ============================================================

AnimatedGIF idleGif;

const char* idleGifFiles[] =
{
    "/blank.gif",
    "/distracted.gif",
    "/left.gif",
    "/shy.gif",
    "/smirk.gif",
    "/speed.gif",
    "/down.gif",
    "/laughing.gif",
    "/right.gif",
    "/shrink.gif",
    "/squint.gif",
    "/weeping.gif"
};

const uint8_t IDLE_GIF_COUNT = sizeof(idleGifFiles) / sizeof(idleGifFiles[0]);

fs::File idleGifFile;
int idleGifCurrent = -1;
int idleGifFrameDelay = 50;
bool idleGifReady = false;
bool idleGifScreenActive = false;
unsigned long idleGifLastFrame = 0;
// blank.gif é a tela padrão durante 2 minutos após a inicialização
// e durante 2 minutos após a perda da conexão Bluetooth.
const unsigned long IDLE_BLANK_TIME = 2UL * 60UL * 1000UL;
unsigned long idleBlankUntil = 0;
bool idleBlankMode = true;


int idleGifX = 0;
int idleGifY = 0;

void startIdleBlankMode()
{
    idleBlankMode = true;
    idleBlankUntil = millis() + IDLE_BLANK_TIME;

    if (idleGifScreenActive)
    {
        idleGif.close();
        idleGifScreenActive = false;
    }

    idleGifCurrent = -1;
    idleGifReady = false;
}

bool isIdleBlankModeActive()
{
    if (idleBlankMode && (long)(idleBlankUntil - millis()) <= 0)
        idleBlankMode = false;

    return idleBlankMode;
}

void* GIFOpenFile(const char* fname, int32_t* pSize)
{
    idleGifFile = LittleFS.open(fname, "r");

    if (!idleGifFile)
    {
        *pSize = 0;
        return nullptr;
    }

    *pSize = (int32_t)idleGifFile.size();
    return (void*)&idleGifFile;
}

void GIFCloseFile(void* pHandle)
{
    fs::File* file = static_cast<fs::File*>(pHandle);

    if (file != nullptr)
        file->close();
}

int32_t GIFReadFile(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen)
{
    fs::File* file = static_cast<fs::File*>(pFile->fHandle);

    if (file == nullptr)
        return 0;

    int32_t bytesToRead = iLen;

    // Evita tentar ler alem do ultimo byte.
    if ((pFile->iSize - pFile->iPos) < bytesToRead)
        bytesToRead = pFile->iSize - pFile->iPos;

    if (bytesToRead <= 0)
        return 0;

    int32_t bytesRead = (int32_t)file->read(pBuf, bytesToRead);
    pFile->iPos = (int32_t)file->position();

    return bytesRead;
}

int32_t GIFSeekFile(GIFFILE* pFile, int32_t iPosition)
{
    fs::File* file = static_cast<fs::File*>(pFile->fHandle);

    if (file == nullptr)
        return 0;

    file->seek(iPosition);
    pFile->iPos = (int32_t)file->position();

    return pFile->iPos;
}

void GIFDraw(GIFDRAW* pDraw)
{
    uint8_t* source = pDraw->pPixels;
    uint16_t* palette = pDraw->pPalette;

    int y = idleGifY + pDraw->iY + pDraw->y;
    int x = idleGifX + pDraw->iX;

    if (y < 0 || y >= SCREEN_HEIGHT)
        return;

    int width = pDraw->iWidth;

    if (x < 0)
    {
        int skip = -x;
        source += skip;
        width -= skip;
        x = 0;
    }

    if (x + width > SCREEN_WIDTH)
        width = SCREEN_WIDTH - x;

    if (width <= 0)
        return;

    static uint16_t lineBuffer[SCREEN_WIDTH];

    for (int i = 0; i < width; i++)
    {
        uint8_t pixel = source[i];

        if (pDraw->ucHasTransparency &&
            pixel == pDraw->ucTransparent)
        {
            // Em transparencia, preserva o que ja esta na tela.
            continue;
        }

        lineBuffer[i] = palette[pixel];
    }

    if (pDraw->ucHasTransparency)
    {
        // Preserva transparencia, mas evita drawPixel() individual.
        // Agrupamos pixels consecutivos e enviamos cada trecho de uma
        // vez ao TFT, reduzindo bastante o custo da animacao.
        tft.startWrite();

        int runStart = -1;

        for (int i = 0; i < width; i++)
        {
            uint8_t pixel = source[i];
            bool visible = (pixel != pDraw->ucTransparent);

            if (visible)
            {
                lineBuffer[i] = palette[pixel];

                if (runStart < 0)
                    runStart = i;
            }
            else if (runStart >= 0)
            {
                tft.pushImage(
                    x + runStart,
                    y,
                    i - runStart,
                    1,
                    &lineBuffer[runStart]
                );
                runStart = -1;
            }
        }

        if (runStart >= 0)
        {
            tft.pushImage(
                x + runStart,
                y,
                width - runStart,
                1,
                &lineBuffer[runStart]
            );
        }

        tft.endWrite();
    }
    else
    {
        tft.pushImage(x, y, width, 1, lineBuffer);
    }
}

int chooseIdleGif()
{
    if (IDLE_GIF_COUNT <= 1)
        return 0;

    int selected;

    do
    {
        selected = random(IDLE_GIF_COUNT);
    }
    while (selected == idleGifCurrent);

    return selected;
}

void stopIdleGif()
{
    if (idleGifReady)
    {
        idleGif.close();
        idleGifReady = false;
    }

    idleGifScreenActive = false;
    idleGifLastFrame = 0;
}

bool startIdleGif()
{
    stopIdleGif();

    if (!idleGifReady)
        idleGif.begin(LITTLE_ENDIAN_PIXELS);

    // Enquanto o período de blank estiver ativo, o GIF escolhido é sempre
    // o blank.gif (índice 0). Fora desse período, sorteia um dos demais.
    if (isIdleBlankModeActive())
        idleGifCurrent = 0;
    else
        idleGifCurrent = chooseIdleGif();

    const char* filename = idleGifFiles[idleGifCurrent];

    if (!idleGif.open(
        filename,
        GIFOpenFile,
        GIFCloseFile,
        GIFReadFile,
        GIFSeekFile,
        GIFDraw
    ))
    {
        Serial.print("Falha ao abrir GIF: ");
        Serial.println(filename);
        idleGifReady = false;
        idleGifScreenActive = false;
        return false;
    }

    idleGifReady = true;
    idleGifScreenActive = true;
    idleGifFrameDelay = 50;
    idleGifLastFrame = 0;

    int gifWidth = idleGif.getCanvasWidth();
    int gifHeight = idleGif.getCanvasHeight();

    // Centraliza GIFs menores. Se forem maiores, ficam recortados.
    idleGifX = (SCREEN_WIDTH - gifWidth) / 2;
    idleGifY = (SCREEN_HEIGHT - gifHeight) / 2;

    tft.fillScreen(TFT_BLACK);

    Serial.print("GIF idle: ");
    Serial.print(filename);
    Serial.print(" (");
    Serial.print(gifWidth);
    Serial.print("x");
    Serial.print(gifHeight);
    Serial.println(")");

    return true;
}

void drawIdleGifFrame()
{
    if (!idleGifScreenActive || !idleGifReady)
        return;

    if (millis() - idleGifLastFrame < (unsigned long)idleGifFrameDelay)
        return;

    idleGifLastFrame = millis();

    if (!idleGif.playFrame(false, &idleGifFrameDelay))
    {
        // Ao terminar qualquer GIF diferente do blank, inicia novamente
        // o período de 5 minutos com blank.gif.
        if (idleGifCurrent != 0)
        {
            startIdleBlankMode();
            startIdleGif();
        }
        else
        {
            // blank.gif permanece sendo repetido enquanto os 5 minutos
            // ainda não tiverem terminado. Quando terminar, o próximo
            // GIF será sorteado normalmente.
            startIdleGif();
        }
        return;
    }

    // O GIF ocupa a tela inteira. O icone Bluetooth foi removido
    // para reduzir trabalho de desenho durante a animacao.
}

// ============================================================
// BLE
// ============================================================

NimBLEClient* pClient = nullptr;
NimBLEServer* pServerGlobal = nullptr;

NimBLERemoteService* pANCSService = nullptr;

NimBLERemoteCharacteristic* pNotificationSource = nullptr;

NimBLERemoteCharacteristic* pControlPoint = nullptr;

NimBLERemoteCharacteristic* pDataSource = nullptr;

NimBLERemoteService* pAMSService = nullptr;

NimBLERemoteCharacteristic* pAMSEntityUpdate = nullptr;

NimBLERemoteCharacteristic* pAMSEntityAttribute = nullptr;


// ============================================================
// ESTADO BLE
// ============================================================

volatile bool bleConnected = false;

// Estado visual do Bluetooth:
// 0 = ativo/anunciando, aguardando conexão
// 1 = conectado
// 2 = não conectado / conexão perdida
volatile uint8_t bluetoothDisplayState = 0;

volatile bool ancsReady = false;

volatile bool ancsDiscoveryRequested = false;


// ============================================================
// ESTADO DE PROCESSAMENTO
// ============================================================

volatile bool requestAttributesPending = false;

volatile bool dataBufferReady = false;

volatile bool displayUpdatePending = false;


// ============================================================
// MUTEX
//
// IMPORTANTE:
// Deve ficar declarado ANTES dos callbacks.
// ============================================================

portMUX_TYPE dataMux =
    portMUX_INITIALIZER_UNLOCKED;


// ============================================================
// UID DA NOTIFICAÇÃO
// ============================================================

uint8_t notificationUID[4] = {
    0,
    0,
    0,
    0
};


// ============================================================
// BUFFER DATA SOURCE
// ============================================================

#define DATA_BUFFER_SIZE 1024

uint8_t dataBuffer[DATA_BUFFER_SIZE];

volatile size_t dataBufferLength = 0;


// ============================================================
// DADOS DA NOTIFICAÇÃO
// ============================================================

String notificationApp = "";

String notificationTitle = "";

String notificationSubtitle = "";

String notificationMessage = "";


// ============================================================
// TIPO DA NOTIFICAÇÃO
// ============================================================


// Tipo visual da notificação:
// 0 = comum
// 1 = mídia
// 2 = WhatsApp
// 3 = Instagram
int currentNotificationVisualType = 0;

// Categoria ANCS recebida no Notification Source.
uint8_t currentNotificationCategory = 0;

// ============================================================
// APPLE MEDIA SERVICE - NOW PLAYING
// ============================================================

String mediaPlayerName = "";
String mediaTrackTitle = "";
String mediaTrackArtist = "";
String mediaTrackAlbum = "";

volatile bool amsReady = false;
volatile unsigned long lastAMSDiscoveryAttempt = 0;

// Player: 0=Name, 1=Playback State, 2=Volume
// Track:  0=Artist, 1=Album, 2=Title, 3=Duration

#define AMS_ENTITY_PLAYER 0x00
#define AMS_ENTITY_TRACK  0x02

#define AMS_PLAYER_NAME   0x00
#define AMS_PLAYER_STATE  0x01

#define AMS_TRACK_ARTIST  0x00
#define AMS_TRACK_ALBUM   0x01
#define AMS_TRACK_TITLE   0x02

uint8_t mediaPlaybackState = 0;

// A tela de mídia só é ativada quando uma nova faixa é detectada.
// O AMS pode continuar enviando dados da faixa antiga mesmo depois
// que a reprodução parou; por isso não usamos apenas mediaTrackTitle.
bool mediaScreenActive = false;
bool mediaTrackChangedPending = false;

// Evita que o PlaybackState transitório recebido durante a troca
// de faixa desligue a tela logo após o novo título chegar.
unsigned long mediaTrackChangeTime = 0;
#define MEDIA_TRACK_STATE_GRACE 8000

// ============================================================
// MARQUEE / SCROLL LATERAL
// ============================================================
// Textos longos de mídia são exibidos em uma única linha e
// deslizam horizontalmente para permitir fontes maiores.
unsigned long marqueeLastUpdate = 0;
int marqueeTitleOffset = 0;
int marqueeArtistOffset = 0;
String marqueeTitleCache = "";
String marqueeArtistCache = "";

// Scroll lateral das notificações
unsigned long notificationMarqueeLastUpdate = 0;
int notificationSenderOffset = 0;
int notificationMessageOffset = 0;
String notificationSenderCache = "";
String notificationMessageCache = "";

#define MARQUEE_INTERVAL 35
#define MARQUEE_SPEED 2
#define MARQUEE_GAP 35


// ============================================================
// TEMPO DA NOTIFICAÇÃO
// ============================================================

unsigned long lastNotificationTime = 0;

// Etapa da notificação: primeiro anuncia o aplicativo, depois mostra
// remetente + conteúdo da mensagem.
volatile bool notificationContentStage = false;

// Waze e Google Maps mostram somente o texto da notificação.
// Não passam pela tela de anúncio do aplicativo.
bool navigationNotification = false;

// Notificações comuns ficam na tela por esse tempo.
#define NORMAL_NOTIFICATION_TIMEOUT 10000
#define NOTIFICATION_ANNOUNCEMENT_TIME 2500


// ============================================================
// IDENTIFICA MÍDIA
// ============================================================
//
// ANCS CategoryID 11 = Entertainment.
// Também reconhecemos alguns Bundle IDs comuns de mídia.
// ============================================================

bool isMediaApp(String appId)
{
    if (appId == "com.apple.Music")
        return true;

    if (appId == "com.spotify.client")
        return true;

    if (appId == "com.google.ios.youtube")
        return true;

    if (appId == "com.apple.podcasts")
        return true;

    if (appId == "com.apple.tv")
        return true;

    return false;
}


// ============================================================
// CLASSIFICA A NOTIFICAÇÃO
// ============================================================

void classifyNotification()
{
    currentNotificationVisualType = 0;

    if (notificationApp == "net.whatsapp.WhatsApp")
    {
        currentNotificationVisualType = 2;
        return;
    }

    if (notificationApp == "com.burbn.instagram")
    {
        currentNotificationVisualType = 3;
        return;
    }

    // Entertainment / mídia.
    if (currentNotificationCategory == 11 ||
        isMediaApp(notificationApp))
    {
        currentNotificationVisualType = 1;
        return;
    }
}


// ============================================================
// COR DA FAIXA DA NOTIFICAÇÃO
// ============================================================

uint16_t getNotificationBandColor()
{
    // Instagram: laranja
    if (notificationApp == "com.burbn.instagram")
        return TFT_ORANGE;

    // Apple Podcasts: roxo
    if (notificationApp == "com.apple.podcasts")
        return TFT_PURPLE;

    switch (currentNotificationVisualType)
    {
        case 1:
            return TFT_RED;

        case 2:
            return TFT_GREEN;

        case 3:
            return TFT_ORANGE;

        default:
            return TFT_DARKGREY;
    }
}


// ============================================================
// NOME DO REMETENTE
// ============================================================
//
// Para notificações ANCS, o Title normalmente contém o nome
// da pessoa/assunto principal.
// ============================================================

String getNotificationSender()
{
    if (notificationTitle.length() > 0)
        return notificationTitle;

    if (notificationSubtitle.length() > 0)
        return notificationSubtitle;

    return "";
}


// ============================================================
// TEXTO DA MÍDIA
// ============================================================

String getMediaTitle()
{
    if (mediaTrackTitle.length() > 0)
        return mediaTrackTitle;

    if (notificationTitle.length() > 0)
        return notificationTitle;

    if (notificationMessage.length() > 0)
        return notificationMessage;

    return "";
}


String getMediaArtist()
{
    if (mediaTrackArtist.length() > 0)
        return mediaTrackArtist;

    if (mediaTrackAlbum.length() > 0)
        return mediaTrackAlbum;

    if (notificationSubtitle.length() > 0)
        return notificationSubtitle;

    return "";
}


// ============================================================
// CONVERTE APP ID EM NOME AMIGÁVEL
// ============================================================

String getAppName(
    String appId
)
{
    // --------------------------------------------------------
    // Apple Maps
    // --------------------------------------------------------

    if (
        appId == "com.apple.Maps"
    )
    {
        return "APPLE MAPS";
    }


    // --------------------------------------------------------
    // Waze
    // --------------------------------------------------------

    if (
        appId == "com.waze.iphone"
    )
    {
        return "WAZE";
    }


    // --------------------------------------------------------
    // Google Maps
    // --------------------------------------------------------

    if (
        appId == "com.google.Maps"
    )
    {
        return "GOOGLE MAPS";
    }


    // --------------------------------------------------------
    // Instagram
    // --------------------------------------------------------

    if (
        appId == "com.burbn.instagram"
    )
    {
        return "INSTAGRAM";
    }


    // --------------------------------------------------------
    // WhatsApp
    // --------------------------------------------------------

    if (
        appId == "net.whatsapp.WhatsApp"
    )
    {
        return "WHATSAPP";
    }


    // --------------------------------------------------------
    // Telegram
    // --------------------------------------------------------

    if (
        appId == "ph.telegra.Telegraph"
    )
    {
        return "TELEGRAM";
    }


    // --------------------------------------------------------
    // iMessage
    // --------------------------------------------------------

    if (
        appId == "com.apple.MobileSMS"
    )
    {
        return "MENSAGENS";
    }


    // --------------------------------------------------------
    // Telefone
    // --------------------------------------------------------

    if (
        appId == "com.apple.mobilephone"
    )
    {
        return "TELEFONE";
    }


    // --------------------------------------------------------
    // Se não conhecemos o aplicativo,
    // devolvemos o Bundle ID.
    // --------------------------------------------------------

    return appId;
}


// ============================================================
// MAIÚSCULAS
// ============================================================

String upperText(
    String text
)
{
    text.toUpperCase();

    return text;
}


// ============================================================
// CONTÉM TEXTO
// ============================================================

bool containsText(
    String text,
    String search
)
{
    text.toUpperCase();

    search.toUpperCase();

    return text.indexOf(
        search
    ) >= 0;
}


// ============================================================
// CONVERTE BUFFER PARA STRING
// ============================================================

String bufferToString(
    const uint8_t* data,
    size_t length
)
{
    String result = "";


    for (
        size_t i = 0;
        i < length;
        i++
    )
    {
        if (
            data[i] == 0
        )
        {
            break;
        }


        if (
            data[i] >= 32
        )
        {
            result +=
                (char)data[i];
        }
    }


    return result;
}


bool isAllowedNotificationApp(
    String appId
)
{
    // WhatsApp
    if (
        appId == "net.whatsapp.WhatsApp"
    )
    {
        return true;
    }

    // Instagram
    if (
        appId == "com.burbn.instagram"
    )
    {
        return true;
    }

    // SMS / Mensagens
    if (
        appId == "com.apple.MobileSMS"
    )
    {
        return true;
    }

    // Chamadas telefonicas
    if (
        appId == "com.apple.mobilephone"
    )
    {
        return true;
    }

    // Waze
    if (
        appId == "com.waze.iphone"
    )
    {
        return true;
    }

    // Google Maps
    if (
        appId == "com.google.Maps"
    )
    {
        return true;
    }

    return false;
}


String getNotificationText()
{
    if (
        notificationMessage.length() > 0
    )
    {
        return notificationMessage;
    }


    if (
        notificationTitle.length() > 0
    )
    {
        return notificationTitle;
    }


    if (
        notificationSubtitle.length() > 0
    )
    {
        return notificationSubtitle;
    }


    return "";
}


String asciiText(String text)
{
    String out = "";

    for (int i = 0; i < text.length(); i++)
    {
        uint8_t c = (uint8_t)text[i];

        // ASCII normal
        if (c < 128)
        {
            if (c >= 32 || c == '\n')
                out += (char)c;
            continue;
        }

        // UTF-8: substituicoes dos principais caracteres portugueses
        if (c == 0xC3 && i + 1 < text.length())
        {
            uint8_t d = (uint8_t)text[++i];

            switch (d)
            {
                // a/A
                case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5:
                    out += 'a'; break;
                case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85:
                    out += 'A'; break;

                // c/C
                case 0x87: out += 'C'; break;
                case 0xA7: out += 'c'; break;

                // e/E
                case 0x88: case 0x89: case 0x8A: case 0x8B:
                    out += 'E'; break;
                case 0xA8: case 0xA9: case 0xAA: case 0xAB:
                    out += 'e'; break;

                // i/I
                case 0x8C: case 0x8D: case 0x8E: case 0x8F:
                    out += 'I'; break;
                case 0xAC: case 0xAD: case 0xAE: case 0xAF:
                    out += 'i'; break;

                // n/N
                case 0x91: out += 'N'; break;
                case 0xB1: out += 'n'; break;

                // o/O
                case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x98:
                    out += 'O'; break;
                case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB8:
                    out += 'o'; break;

                // u/U
                case 0x99: case 0x9A: case 0x9B: case 0x9C:
                    out += 'U'; break;
                case 0xB9: case 0xBA: case 0xBB: case 0xBC:
                    out += 'u'; break;

                // y/Y
                case 0x9D: out += 'Y'; break;
                case 0xBD: out += 'y'; break;

                default:
                    out += '?';
                    break;
            }
            continue;
        }

        // UTF-8 de pontuacao comum
        if (c == 0xE2 && i + 2 < text.length())
        {
            uint8_t d = (uint8_t)text[++i];
            uint8_t e = (uint8_t)text[++i];

            if (d == 0x80 && (e == 0x93 || e == 0x94)) out += '-';
            else if (d == 0x80 && (e == 0x98 || e == 0x99 || e == 0x9C || e == 0x9D)) out += '"';
            else if (d == 0x80 && e == 0xA2) out += '*';
            else if (d == 0x80 && e == 0xA6) out += '.';
            else out += '?';
            continue;
        }

        out += '?';
    }

    return out;
}


void setupTextStyle(uint16_t color, uint8_t size)
{
    tft.setTextFont(1);
    tft.setTextSize(size);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextWrap(false);
}


int textWidthSimple(String text, uint8_t size)
{
    text = asciiText(text);
    setupTextStyle(TFT_WHITE, size);
    return tft.textWidth(text);
}


void drawCenteredText(String text, int y, uint16_t color, uint8_t size)
{
    text = asciiText(text);
    setupTextStyle(color, size);

    int w = tft.textWidth(text);
    int x = (SCREEN_WIDTH - w) / 2;

    if (x < 0) x = 0;

    tft.setCursor(x, y);
    tft.print(text);
}


void drawCenteredTextTransparent(String text, int y, uint16_t color, uint8_t size)
{
    text = asciiText(text);
    tft.setTextFont(1);
    tft.setTextSize(size);
    tft.setTextColor(color);
    tft.setTextWrap(false);

    int w = tft.textWidth(text);
    int x = (SCREEN_WIDTH - w) / 2;

    if (x < 0) x = 0;

    tft.setCursor(x, y);
    tft.print(text);
}


void drawWrappedText(
    String text,
    int x,
    int y,
    int maxWidth,
    int lineHeight,
    uint16_t color,
    int textSize
)
{
    text = asciiText(text);

    uint8_t size = 1;
    if (textSize >= 3) size = 2;
    else if (textSize >= 2) size = 2;

    setupTextStyle(color, size);

    int charHeight = 8 * size;
    if (lineHeight < charHeight + 2)
        lineHeight = charHeight + 2;

    int maxLines = (SCREEN_HEIGHT - y - 2) / lineHeight;
    if (maxLines < 1) return;

    String line = "";
    int currentY = y;
    int lines = 0;

    for (int i = 0; i <= text.length() && lines < maxLines; i++)
    {
        bool end = (i == text.length());
        char c = end ? ' ' : text[i];

        if (c == '\n' || end)
        {
            if (line.length() > 0)
            {
                tft.setCursor(x, currentY);
                tft.print(line);
                lines++;
                currentY += lineHeight;
            }
            line = "";
            continue;
        }

        String candidate = line + c;

        if (tft.textWidth(candidate) <= maxWidth)
        {
            line = candidate;
        }
        else
        {
            // Tenta quebrar por espaco.
            int lastSpace = line.lastIndexOf(' ');

            if (lastSpace >= 0)
            {
                String toDraw = line.substring(0, lastSpace);
                tft.setCursor(x, currentY);
                tft.print(toDraw);
                lines++;
                currentY += lineHeight;

                line = line.substring(lastSpace + 1);
                line += c;
            }
            else
            {
                tft.setCursor(x, currentY);
                tft.print(line);
                lines++;
                currentY += lineHeight;
                line = String(c);
            }
        }
    }
}


void drawANCSIndicator()
{
    if (!ancsReady) return;
    drawCenteredText("<<", 1, TFT_GREEN, 1);
}


// ============================================================
// DESENHA FAIXA DO APLICATIVO
// ============================================================

void drawNotificationAppBand()
{
    uint16_t bandColor = getNotificationBandColor();

    // Durante o anúncio, o aplicativo ocupa a tela inteira.
    // O nome usa fonte preta sobre a cor do aplicativo.
    tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, bandColor);

    String appName = getAppName(notificationApp);
    drawCenteredTextTransparent(
        appName,
        (SCREEN_HEIGHT - 32) / 2,
        TFT_BLACK,
        4
    );
}


// ============================================================
// TELA DE MÍDIA
// ============================================================

void drawScrollingText(
    String text,
    int y,
    int areaHeight,
    uint16_t color,
    uint8_t size,
    int &offset,
    String &cache
)
{
    text = asciiText(text);

    // O fundo é desenhado pela área chamadora.
    // Isso permite texto preto sobre a faixa colorida sem criar
    // uma tarja preta atrás das letras.
    tft.setTextFont(1);
    tft.setTextSize(size);
    tft.setTextColor(color);
    tft.setTextWrap(false);

    int textWidth = tft.textWidth(text);
    int visibleWidth = SCREEN_WIDTH - 16;
    int x = 8;

    // Se o texto cabe, centraliza normalmente.
    if (textWidth <= visibleWidth)
    {
        int centeredX = (SCREEN_WIDTH - textWidth) / 2;
        int yText = y + (areaHeight - 8 * size) / 2;
        if (yText < y) yText = y;

        tft.setCursor(centeredX, yText);
        tft.print(text);
        offset = 0;
        cache = text;
        return;
    }

    // Reinicia o scroll quando muda a faixa/artista.
    if (cache != text)
    {
        cache = text;
        offset = 0;
    }

    int cycleWidth = textWidth + MARQUEE_GAP;
    int xScroll = x - offset;
    int yText = y + (areaHeight - 8 * size) / 2;
    if (yText < y) yText = y;

    // Desenha duas cópias para criar um scroll contínuo.
    tft.setCursor(xScroll, yText);
    tft.print(text);

    if (xScroll + textWidth < SCREEN_WIDTH)
    {
        tft.setCursor(xScroll + cycleWidth, yText);
        tft.print(text);
    }

    offset += MARQUEE_SPEED;
    if (offset >= cycleWidth)
        offset = 0;
}


uint16_t getMediaBandColor()
{
    String app = mediaPlayerName;
    app.toLowerCase();

    if (app.indexOf("podcast") >= 0)
        return TFT_PURPLE;

    if (app.indexOf("instagram") >= 0)
        return TFT_ORANGE;

    return TFT_RED;
}


void drawMediaScreen()
{
    stopIdleGif();
    tft.fillScreen(TFT_BLACK);
    drawANCSIndicator();

    tft.fillRect(0, 0, SCREEN_WIDTH, 34, getMediaBandColor());

    String mediaApp = mediaPlayerName;
    if (mediaApp.length() == 0) mediaApp = "MIDIA";

    drawCenteredTextTransparent(mediaApp, 5, TFT_WHITE, 3);

    String title = getMediaTitle();
    String artist = getMediaArtist();
    if (title.length() == 0) title = "(sem titulo)"; // TÍTULO
    // Fonte 4. Se não couber, faz scroll lateral.
    setupTextStyle(TFT_WHITE, 4);
    int titleWidth = tft.textWidth(asciiText(title));

    tft.fillRect(0, 40, SCREEN_WIDTH, 45, TFT_BLACK);
    drawScrollingText(
        title,
        44,
        38,
        TFT_WHITE,
        4,
        marqueeTitleOffset,
        marqueeTitleCache
    ); // ARTISTA
    // Fonte 3. Também usa scroll lateral quando necessário.
    tft.fillRect(0, 88, SCREEN_WIDTH, 35, TFT_BLACK);

    if (artist.length() > 0)
    {
        drawScrollingText(
            artist,
            91,
            28,
            TFT_LIGHTGREY,
            3,
            marqueeArtistOffset,
            marqueeArtistCache
        );
    }
}


void updateMediaMarquee()
{
    // Enquanto uma notificacao estiver ativa, a musica nao pode
    // redesenhar a tela por cima da notificacao.
    if (
        lastNotificationTime > 0 &&
        millis() - lastNotificationTime < NORMAL_NOTIFICATION_TIMEOUT
    )
    {
        return;
    }

    if (!mediaScreenActive ||
        mediaTrackTitle.length() == 0)
    {
        return;
    }

    if (millis() - marqueeLastUpdate < MARQUEE_INTERVAL)
        return;

    marqueeLastUpdate = millis();

    // Não redesenha a tela inteira. Atualiza apenas as áreas móveis.
    String title = getMediaTitle();
    String artist = getMediaArtist();
    if (title.length() == 0) title = "(sem titulo)";

    tft.fillRect(0, 40, SCREEN_WIDTH, 45, TFT_BLACK);
    drawScrollingText(
        title,
        44,
        38,
        TFT_WHITE,
        4,
        marqueeTitleOffset,
        marqueeTitleCache
    );

    tft.fillRect(0, 88, SCREEN_WIDTH, 35, TFT_BLACK);
    if (artist.length() > 0)
    {
        drawScrollingText(
            artist,
            91,
            28,
            TFT_LIGHTGREY,
            3,
            marqueeArtistOffset,
            marqueeArtistCache
        );
    }
}



// ============================================================
// TELA DE NOTIFICAÇÃO COMUM
// ============================================================

void drawNormalNotificationScreen()
{
    stopIdleGif();
    tft.fillScreen(TFT_BLACK);
    drawANCSIndicator();

    const int bandHeight = SCREEN_HEIGHT / 2;
    uint16_t bandColor = getNotificationBandColor();

    // --------------------------------------------------------
    // Waze / Google Maps: sem anúncio do aplicativo.
    // --------------------------------------------------------
    if (navigationNotification)
    {
        notificationContentStage = true;
    }

    // --------------------------------------------------------
    // PRIMEIRA ETAPA: anúncio do aplicativo.
    // O aplicativo ocupa a tela inteira.
    // --------------------------------------------------------
    if (!notificationContentStage)
    {
        tft.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, bandColor);

        String appName = getAppName(notificationApp);
        drawCenteredTextTransparent(
            appName,
            (SCREEN_HEIGHT - 32) / 2,
            TFT_BLACK,
            4
        );

        return;
    }

    // --------------------------------------------------------
    // SEGUNDA ETAPA: contato/remetente na faixa colorida e
    // conteúdo da notificação no restante da tela.
    // --------------------------------------------------------
    tft.fillRect(0, 0, SCREEN_WIDTH, bandHeight, bandColor);

    String sender = getNotificationSender();
    if (sender.length() == 0)
        sender = "(sem contato)";

    drawScrollingText(
        sender,
        0,
        bandHeight,
        TFT_BLACK,
        4,
        notificationSenderOffset,
        notificationSenderCache
    );

    const int messageY = bandHeight;
    const int messageHeight = SCREEN_HEIGHT - messageY;

    String message = getNotificationText();
    if (message.length() == 0)
        message = "(sem mensagem)";

    // Parte inferior: sempre preta, com texto branco.
    uint16_t messageBackground = TFT_BLACK;
    uint16_t messageTextColor = TFT_WHITE;

    tft.fillRect(0, messageY, SCREEN_WIDTH, messageHeight, messageBackground);

    drawScrollingText(
        message,
        messageY,
        messageHeight,
        messageTextColor,
        3,
        notificationMessageOffset,
        notificationMessageCache
    );
}


void updateNotificationMarquee()
{
    if (!bleConnected ||
        lastNotificationTime == 0)
    {
        return;
    }

    unsigned long elapsed = millis() - lastNotificationTime;

    if (elapsed >= NORMAL_NOTIFICATION_TIMEOUT)
    {
        return;
    }

    // Waze / Google Maps não possuem etapa de anúncio.
    if (navigationNotification)
    {
        notificationContentStage = true;
    }

    // Passa do anúncio para o conteúdo da notificação.
    if (!navigationNotification &&
        !notificationContentStage &&
        elapsed >= NOTIFICATION_ANNOUNCEMENT_TIME)
    {
        notificationContentStage = true;
        notificationSenderOffset = 0;
        notificationMessageOffset = 0;
        notificationSenderCache = "";
        notificationMessageCache = "";
        drawNormalNotificationScreen();
        return;
    }

    if (millis() - notificationMarqueeLastUpdate < MARQUEE_INTERVAL)
        return;

    notificationMarqueeLastUpdate = millis();

    // Durante o anúncio não há texto para deslocar.
    if (!notificationContentStage)
        return;

    const int bandHeight = SCREEN_HEIGHT / 2;
    String sender = getNotificationSender();
    if (sender.length() == 0)
        sender = "(sem contato)";

    String message = getNotificationText();
    if (message.length() == 0)
        message = "(sem mensagem)";

    // Atualiza apenas as duas áreas móveis.
    tft.fillRect(0, 0, SCREEN_WIDTH, bandHeight, getNotificationBandColor());
    drawScrollingText(
        sender,
        0,
        bandHeight,
        TFT_BLACK,
        4,
        notificationSenderOffset,
        notificationSenderCache
    );

    const int messageY = bandHeight;
    const int messageHeight = SCREEN_HEIGHT - messageY;

    // Parte inferior: sempre preta, com texto branco.
    uint16_t messageBackground = TFT_BLACK;
    uint16_t messageTextColor = TFT_WHITE;

    tft.fillRect(0, messageY, SCREEN_WIDTH, messageHeight, messageBackground);

    drawScrollingText(
        message,
        messageY,
        messageHeight,
        messageTextColor,
        3,
        notificationMessageOffset,
        notificationMessageCache
    );
}


// ============================================================
// LOGO PADRÃO + STATUS BLUETOOTH
// ============================================================



// ============================================================
// MOSTRA LOGO PADRÃO
// ============================================================

void drawDefaultLogo()
{
    // A antiga logo PROGMEM deixa de ser usada como tela de idle.
    // Agora o idle e composto por um GIF aleatorio.
    if (!idleGifScreenActive)
    {
        if (!startIdleGif())
        {
            // Sem GIF disponivel, mantem a tela preta.
            tft.fillScreen(TFT_BLACK);
        }
    }
}


// Compatibilidade com chamadas antigas.
// Ambas agora usam a mesma logo padrão; o estado do Bluetooth
// determina apenas o ícone exibido no rodapé.
void drawConnectedLogo()
{
    bluetoothDisplayState = 1;
    drawDefaultLogo();
}

void drawDisconnectedLogo()
{
    drawDefaultLogo();
}

// ============================================================
// ATUALIZA DISPLAY
// ============================================================

void updateDisplay()
{
    // --------------------------------------------------------
    // Sem conexão ANCS/AMS:
    // mantém a logo padrão com o estado atual do Bluetooth.
    // --------------------------------------------------------

    if (!bleConnected)
    {
        drawDefaultLogo();
        return;
    }


    // --------------------------------------------------------
    // Now Playing.
    //
    // IMPORTANTE: mediaTrackTitle sozinho NÃO significa que há
    // música tocando. O AMS mantém os últimos dados conhecidos.
    // Portanto, somente mostramos mídia quando mediaScreenActive
    // foi marcado por uma mudança real de faixa.
    // --------------------------------------------------------

    // --------------------------------------------------------
    // Notificação comum tem prioridade enquanto estiver ativa.
    // --------------------------------------------------------

    if (
        lastNotificationTime > 0 &&
        millis() - lastNotificationTime <
        NORMAL_NOTIFICATION_TIMEOUT
    )
    {
        drawNormalNotificationScreen();
        return;
    }


    // --------------------------------------------------------
    // Now Playing. Após a notificação expirar, a música retorna
    // automaticamente sem perder o estado da faixa.
    // --------------------------------------------------------

    if (
        mediaScreenActive &&
        mediaTrackTitle.length() > 0
    )
    {
        drawMediaScreen();
        return;
    }


    // --------------------------------------------------------
    // Sem notificação e sem música:
    // sempre volta para a logo padrão.
    // --------------------------------------------------------

    drawDefaultLogo();
}


// ============================================================
// CALLBACK NOTIFICATION SOURCE
// ============================================================

void notificationSourceCallback(
    NimBLERemoteCharacteristic* pCharacteristic,
    uint8_t* pData,
    size_t length,
    bool isNotify
)
{
    if (
        length < 8
    )
    {
        return;
    }


    uint8_t eventID =
        pData[0];

    // Byte 2 do Notification Source = Category ID.
    currentNotificationCategory =
        pData[2];


    // --------------------------------------------------------
    // UID
    // --------------------------------------------------------

    portENTER_CRITICAL(
        &dataMux
    );


    notificationUID[0] =
        pData[4];


    notificationUID[1] =
        pData[5];


    notificationUID[2] =
        pData[6];


    notificationUID[3] =
        pData[7];


    portEXIT_CRITICAL(
        &dataMux
    );


    // --------------------------------------------------------
    // EVENT ID 0 = Added
    //
    // Solicitamos os atributos.
    // --------------------------------------------------------

    if (
        eventID == 0
    )
    {
        requestAttributesPending =
            true;
    }
}


// ============================================================
// CALLBACK DATA SOURCE
// ============================================================

void dataSourceCallback(
    NimBLERemoteCharacteristic* pCharacteristic,
    uint8_t* pData,
    size_t length,
    bool isNotify
)
{
    if (
        length == 0
    )
    {
        return;
    }


    if (
        length > DATA_BUFFER_SIZE
    )
    {
        length =
            DATA_BUFFER_SIZE;
    }


    portENTER_CRITICAL(
        &dataMux
    );


    memcpy(
        dataBuffer,
        pData,
        length
    );


    dataBufferLength =
        length;


    dataBufferReady =
        true;


    portEXIT_CRITICAL(
        &dataMux
    );
}


// ============================================================
// PROCESSA DATA SOURCE
// ============================================================

void processDataSource()
{
    uint8_t localBuffer[
        DATA_BUFFER_SIZE
    ];


    size_t localLength;


    // --------------------------------------------------------
    // Copia o buffer de forma segura.
    // --------------------------------------------------------

    portENTER_CRITICAL(
        &dataMux
    );


    localLength =
        dataBufferLength;


    memcpy(
        localBuffer,
        dataBuffer,
        localLength
    );


    dataBufferReady =
        false;


    portEXIT_CRITICAL(
        &dataMux
    );


    if (
        localLength < 5
    )
    {
        return;
    }


    // --------------------------------------------------------
    // Command ID
    // --------------------------------------------------------

    uint8_t commandID =
        localBuffer[0];


    if (
        commandID != 0
    )
    {
        return;
    }


    // --------------------------------------------------------
    // UID
    // --------------------------------------------------------

    uint32_t uid =
        ((uint32_t)localBuffer[1]) |
        ((uint32_t)localBuffer[2] << 8) |
        ((uint32_t)localBuffer[3] << 16) |
        ((uint32_t)localBuffer[4] << 24);


    Serial.println();

    Serial.println(
        "========= ANCS DATA ========="
    );


    Serial.print(
        "UID: "
    );


    Serial.println(
        uid
    );


    // --------------------------------------------------------
    // Limpa os dados anteriores
    // --------------------------------------------------------

    notificationApp =
        "";


    notificationTitle =
        "";


    notificationSubtitle =
        "";


    notificationMessage =
        "";


    // --------------------------------------------------------
    // Lê atributos
    // --------------------------------------------------------

    size_t pos =
        5;


    while (
        pos + 3 <= localLength
    )
    {
        uint8_t attributeID =
            localBuffer[pos];


        uint16_t attributeLength =
            ((uint16_t)localBuffer[pos + 1]) |
            ((uint16_t)localBuffer[pos + 2] << 8);


        pos += 3;


        if (
            pos + attributeLength >
            localLength
        )
        {
            Serial.println(
                "Atributo incompleto."
            );

            break;
        }


        String value =
            bufferToString(
                &localBuffer[pos],
                attributeLength
            );


        Serial.print(
            "Attr "
        );


        Serial.print(
            attributeID
        );


        Serial.print(
            ": "
        );


        Serial.println(
            value
        );


        switch (
            attributeID
        )
        {
            case 0:

                notificationApp =
                    value;

                break;


            case 1:

                notificationTitle =
                    value;

                break;


            case 2:

                notificationSubtitle =
                    value;

                break;


            case 3:

                notificationMessage =
                    value;

                break;
        }


        pos +=
            attributeLength;
    }
 // DEBUG

    Serial.println();

    Serial.print(
        "Aplicativo ANCS: "
    );


    Serial.println(
        notificationApp
    );


    Serial.print(
        "Titulo: "
    );


    Serial.println(
        notificationTitle
    );


    Serial.print(
        "Subtitulo: "
    );


    Serial.println(
        notificationSubtitle
    );


    Serial.print(
        "Mensagem: "
    );


    Serial.println(
        notificationMessage
    );
 // FILTRO DE NOTIFICACOES
    //
    // Para notificacoes comuns, WhatsApp, Instagram,
    // Mensagens/SMS, Telefone, Waze e Google Maps podem chegar ao display.
    // Waze e Google Maps entram diretamente na etapa de texto, sem anúncio.
    //
    // IMPORTANTE:
    // O AMS (midia / Now Playing) e independente deste fluxo,
    // portanto Spotify, Apple Music etc. continuam funcionando.

    if (!isAllowedNotificationApp(notificationApp))
    {
        Serial.println();
        Serial.println(
            "Notificacao ignorada pelo filtro de aplicativos."
        );
        Serial.print(
            "App bloqueado: "
        );
        Serial.println(
            notificationApp
        );

        // Nao altera a notificacao atualmente exibida.
        // Assim, uma notificacao de outro app nao apaga
        // WhatsApp/Instagram/SMS/chamada da tela.
        return;
    }
 // IDENTIFICA O TIPO

    // A notificação assume a tela temporariamente, mas NÃO apaga
    // o estado da mídia. Ao terminar, a tela de música retorna
    // automaticamente se houver uma faixa ativa.
    classifyNotification();

    // Navegação: somente texto, sem anúncio do aplicativo.
    navigationNotification =
        (notificationApp == "com.waze.iphone" ||
         notificationApp == "com.google.Maps");

    notificationContentStage = navigationNotification;
    notificationSenderOffset = 0;
    notificationMessageOffset = 0;
    notificationSenderCache = "";
    notificationMessageCache = "";
    notificationMarqueeLastUpdate = 0;

    lastNotificationTime =
        millis();
 // NOTIFICACAO COMUM
        Serial.println();

        Serial.println(
            "========= NOTIFICACAO ========="
        );


        Serial.print(
            "App: "
        );


        Serial.println(
            getAppName(
                notificationApp
            )
        );


        Serial.print(
            "Texto: "
        );


        Serial.println(
            getNotificationText()
        );
 // DISPLAY

    updateDisplay();
}


// ============================================================
// SOLICITA ATRIBUTOS
// ============================================================

void requestNotificationAttributes()
{
    if (
        pControlPoint == nullptr
    )
    {
        return;
    }


    if (
        !ancsReady
    )
    {
        return;
    }


    uint8_t command[32];


    size_t pos =
        0;


    // --------------------------------------------------------
    // Command ID
    // --------------------------------------------------------

    command[pos++] =
        0x00;


    // --------------------------------------------------------
    // UID
    // --------------------------------------------------------

    command[pos++] =
        notificationUID[0];


    command[pos++] =
        notificationUID[1];


    command[pos++] =
        notificationUID[2];


    command[pos++] =
        notificationUID[3];


    // --------------------------------------------------------
    // App Identifier
    // --------------------------------------------------------

    command[pos++] =
        0x00;


    command[pos++] =
        128;


    command[pos++] =
        0;


    // --------------------------------------------------------
    // Title
    // --------------------------------------------------------

    command[pos++] =
        0x01;


    command[pos++] =
        128;


    command[pos++] =
        0;


    // --------------------------------------------------------
    // Subtitle
    // --------------------------------------------------------

    command[pos++] =
        0x02;


    command[pos++] =
        128;


    command[pos++] =
        0;


    // --------------------------------------------------------
    // Message
    // --------------------------------------------------------

    command[pos++] =
        0x03;


    command[pos++] =
        255;


    command[pos++] =
        0;


    Serial.println();

    Serial.println(
        "Solicitando atributos ANCS..."
    );


    bool ok =
        pControlPoint->writeValue(
            command,
            pos,
            true
        );


    if (
        ok
    )
    {
        Serial.println(
            "Get Notification Attributes enviado."
        );
    }
    else
    {
        Serial.println(
            "ERRO ao enviar atributos."
        );
    }
}


// ============================================================
// PROCESSA NOTIFICAÇÃO DO APPLE MEDIA SERVICE
// ============================================================

void amsEntityUpdateCallback(
    NimBLERemoteCharacteristic* pCharacteristic,
    uint8_t* pData,
    size_t length,
    bool isNotify
)
{
    if (length < 4)
    {
        return;
    }

    uint8_t entityID = pData[0];
    uint8_t attributeID = pData[1];
    uint8_t flags = pData[2];

    String value = bufferToString(
        &pData[3],
        length - 3
    );

    Serial.println();
    Serial.println("========= AMS DATA =========");
    Serial.print("Entity: 0x");
    Serial.println(entityID, HEX);
    Serial.print("Attr: 0x");
    Serial.println(attributeID, HEX);
    Serial.print("Flags: 0x");
    Serial.println(flags, HEX);
    Serial.print("Value: ");
    Serial.println(value);

    if (entityID == AMS_ENTITY_PLAYER)
    {
        if (attributeID == AMS_PLAYER_NAME)
        {
            mediaPlayerName = value;
        }
        else if (attributeID == AMS_PLAYER_STATE)
        {
            int comma = value.indexOf(',');
            uint8_t newState;

            if (comma >= 0)
            {
                String stateText = value.substring(0, comma);
                newState = (uint8_t)stateText.toInt();
            }
            else
            {
                newState = (uint8_t)value.toInt();
            }

            mediaPlaybackState = newState;

            if (mediaPlaybackState != 1)
            {
                // Durante a troca de faixa o iOS pode enviar primeiro
                // um estado transitório (ex.: pausa/parada) e só depois
                // consolidar a nova faixa. Não escondemos a nova música
                // durante essa janela.
                if (millis() - mediaTrackChangeTime >= MEDIA_TRACK_STATE_GRACE)
                {
                    mediaScreenActive = false;
                }
            }
            else if (mediaTrackTitle.length() > 0)
            {
                mediaScreenActive = true;
                mediaTrackChangedPending = false;
            }

            Serial.print("PlaybackState numerico: ");
            Serial.println(mediaPlaybackState);
        }
    }
    else if (entityID == AMS_ENTITY_TRACK)
    {
        if (attributeID == AMS_TRACK_ARTIST)
        {
            mediaTrackArtist = value;
            marqueeArtistOffset = 0;
            marqueeArtistCache = "";
        }
        else if (attributeID == AMS_TRACK_ALBUM)
        {
            mediaTrackAlbum = value;
        }
        else if (attributeID == AMS_TRACK_TITLE)
        {
            if (value != mediaTrackTitle && value.length() > 0)
            {
                mediaTrackTitle = value;

                // O título novo identifica a faixa atual. Não esperamos
                // outro PlaybackState para atualizar a tela.
                mediaScreenActive = true;
                mediaTrackChangedPending = true;
                mediaTrackChangeTime = millis();

                marqueeTitleOffset = 0;
                marqueeArtistOffset = 0;
                marqueeTitleCache = "";
                marqueeArtistCache = "";

                Serial.println("NOVA FAIXA DETECTADA - TELA ATUALIZADA.");
            }
        }
    }

    displayUpdatePending = true;
}


// ============================================================
// CONFIGURA APPLE MEDIA SERVICE
// ============================================================

bool setupAMS()
{
    if (pClient == nullptr || !pClient->isConnected())
    {
        return false;
    }

    Serial.println();
    Serial.println("================================");
    Serial.println("       INICIANDO APPLE MEDIA SERVICE");
    Serial.println("================================");

    pAMSService = pClient->getService(AMS_SERVICE_UUID);

    if (pAMSService == nullptr)
    {
        Serial.println("AMS nao publicado pelo iPhone neste momento.");
        amsReady = false;
        return false;
    }

    pAMSEntityUpdate = pAMSService->getCharacteristic(
        AMS_ENTITY_UPDATE_UUID
    );

    pAMSEntityAttribute = pAMSService->getCharacteristic(
        AMS_ENTITY_ATTRIBUTE_UUID
    );

    if (pAMSEntityUpdate == nullptr)
    {
        Serial.println("ERRO: Entity Update do AMS nao encontrado.");
        amsReady = false;
        return false;
    }

    if (!pAMSEntityUpdate->subscribe(
        true,
        amsEntityUpdateCallback
    ))
    {
        Serial.println("ERRO: nao foi possivel assinar Entity Update AMS.");
        amsReady = false;
        return false;
    }

    // --------------------------------------------------------
    // Player: nome do aplicativo + estado de reproducao
    // --------------------------------------------------------

    uint8_t playerCommand[] = {
        AMS_ENTITY_PLAYER,
        AMS_PLAYER_NAME,
        AMS_PLAYER_STATE
    };

    bool playerOK = pAMSEntityUpdate->writeValue(
        playerCommand,
        sizeof(playerCommand),
        true
    );

    Serial.print("AMS Player subscription: ");
    Serial.println(playerOK ? "OK" : "ERRO");

    // --------------------------------------------------------
    // Track: artista + album + titulo
    // --------------------------------------------------------

    uint8_t trackCommand[] = {
        AMS_ENTITY_TRACK,
        AMS_TRACK_ARTIST,
        AMS_TRACK_ALBUM,
        AMS_TRACK_TITLE
    };

    bool trackOK = pAMSEntityUpdate->writeValue(
        trackCommand,
        sizeof(trackCommand),
        true
    );

    Serial.print("AMS Track subscription: ");
    Serial.println(trackOK ? "OK" : "ERRO");

    amsReady = playerOK || trackOK;

    if (amsReady)
    {
        lastAMSDiscoveryAttempt = millis();
        Serial.println("AMS PRONTO - aguardando Now Playing...");
    }

    return amsReady;
}


// ============================================================
// CONFIGURA ANCS
// ============================================================

bool setupANCS()
{
    if (
        pClient == nullptr
    )
    {
        return false;
    }


    if (
        !pClient->isConnected()
    )
    {
        return false;
    }


    Serial.println();

    Serial.println(
        "================================"
    );


    Serial.println(
        "        INICIANDO ANCS"
    );


    Serial.println(
        "================================"
    );


    // --------------------------------------------------------
    // Serviço
    // --------------------------------------------------------

    pANCSService =
        pClient->getService(
            ANCS_SERVICE_UUID
        );


    if (
        pANCSService == nullptr
    )
    {
        Serial.println(
            "ERRO: ANCS nao encontrado."
        );


        return false;
    }


    Serial.println(
        "ANCS encontrado."
    );


    // --------------------------------------------------------
    // Characteristics
    // --------------------------------------------------------

    pNotificationSource =
        pANCSService->getCharacteristic(
            NOTIFICATION_SOURCE_UUID
        );


    pControlPoint =
        pANCSService->getCharacteristic(
            CONTROL_POINT_UUID
        );


    pDataSource =
        pANCSService->getCharacteristic(
            DATA_SOURCE_UUID
        );


    if (
        pNotificationSource == nullptr ||
        pControlPoint == nullptr ||
        pDataSource == nullptr
    )
    {
        Serial.println(
            "ERRO: caracteristicas ANCS incompletas."
        );


        return false;
    }


    // --------------------------------------------------------
    // Data Source
    // --------------------------------------------------------

    if (
        !pDataSource->subscribe(
            true,
            dataSourceCallback
        )
    )
    {
        Serial.println(
            "ERRO: Data Source."
        );


        return false;
    }


    Serial.println(
        "Data Source OK."
    );


    // --------------------------------------------------------
    // Notification Source
    // --------------------------------------------------------

    if (
        !pNotificationSource->subscribe(
            true,
            notificationSourceCallback
        )
    )
    {
        Serial.println(
            "ERRO: Notification Source."
        );


        return false;
    }


    Serial.println(
        "Notification Source OK."
    );


    // --------------------------------------------------------
    // ANCS pronto
    // --------------------------------------------------------

    ancsReady =
        true;


    // O AMS e separado do ANCS e e usado para obter
    // musica/podcast em reproducao no iPhone.
    setupAMS();


    Serial.println();

    Serial.println(
        "================================"
    );


    Serial.println(
        "          ANCS PRONTO"
    );


    Serial.println(
        "================================"
    );


    displayUpdatePending =
        true;


    return true;
}


// ============================================================
// SERVER CALLBACKS
// ============================================================

class ServerCallbacks :
    public NimBLEServerCallbacks
{
public: // CONNECT

    void onConnect(
        NimBLEServer* pServer,
        NimBLEConnInfo& connInfo
    ) override
    {
        bleConnected =
            true;

        bluetoothDisplayState = 1;


        ancsDiscoveryRequested =
            true;


        // ----------------------------------------------------
        // Quando conecta, ainda não temos notificação.
        // ----------------------------------------------------


        currentNotificationVisualType =
            0;

        currentNotificationCategory =
            0;

        amsReady = false;
        lastAMSDiscoveryAttempt = 0;
        mediaPlayerName = "";
        mediaTrackTitle = "";
        mediaTrackArtist = "";
        mediaTrackAlbum = "";
        mediaPlaybackState = 0;
        mediaScreenActive = false;
        mediaTrackChangedPending = false;
        mediaTrackChangeTime = 0;

        lastNotificationTime =
            0;


        Serial.println();

        Serial.println(
            "================================"
        );


        Serial.println(
            "      CLIENTE BLE CONECTADO"
        );


        Serial.println(
            "================================"
        );


        Serial.print(
            "Endereco: "
        );


        Serial.println(
            connInfo.getAddress().toString().c_str()
        );


        // ----------------------------------------------------
        // Obtém o cliente associado
        // ----------------------------------------------------

        pClient =
            pServer->getClient(
                connInfo
            );


        if (
            pClient == nullptr
        )
        {
            Serial.println(
                "ERRO: getClient()."
            );


            return;
        }


        Serial.println(
            "Cliente NimBLE associado."
        );


        // ----------------------------------------------------
        // Mostra logo conectado.
        // ----------------------------------------------------

        drawConnectedLogo();
    }
 // DISCONNECT

    void onDisconnect(
        NimBLEServer* pServer,
        NimBLEConnInfo& connInfo,
        int reason
    ) override
    {
        bleConnected = false;
        startIdleBlankMode();

        bluetoothDisplayState = 2;


        ancsReady =
            false;


        ancsDiscoveryRequested =
            false;


        requestAttributesPending =
            false;


        dataBufferReady =
            false;



        currentNotificationVisualType =
            0;

        currentNotificationCategory =
            0;

        amsReady = false;
        lastAMSDiscoveryAttempt = 0;
        mediaPlayerName = "";
        mediaTrackTitle = "";
        mediaTrackArtist = "";
        mediaTrackAlbum = "";
        mediaPlaybackState = 0;
        mediaScreenActive = false;
        mediaTrackChangedPending = false;
        mediaTrackChangeTime = 0;

        lastNotificationTime =
            0;


        pClient =
            nullptr;


        pANCSService =
            nullptr;


        pNotificationSource =
            nullptr;


        pControlPoint =
            nullptr;


        pDataSource =
            nullptr;

        pAMSService = nullptr;
        pAMSEntityUpdate = nullptr;
        pAMSEntityAttribute = nullptr;

        Serial.println();

        Serial.println(
            "================================"
        );


        Serial.println(
            "       CLIENTE DESCONECTADO"
        );


        Serial.println(
            "================================"
        );


        Serial.print(
            "Motivo: "
        );


        Serial.println(
            reason
        );


        // ----------------------------------------------------
        // Logo de desconectado
        // ----------------------------------------------------

        drawDisconnectedLogo();
    }
};


// ============================================================
// REINICIA A CONEXÃO BLUETOOTH
// ============================================================
void restartBluetoothConnection()
{
    Serial.println();
    Serial.println("BOTAO 1: reiniciando conexao Bluetooth...");

    // Cancela o estado atual de ANCS/AMS.
    bleConnected = false;
    startIdleBlankMode();
    bluetoothDisplayState = 0;
    ancsReady = false;
    ancsDiscoveryRequested = false;
    requestAttributesPending = false;
    dataBufferReady = false;
    displayUpdatePending = false;

    amsReady = false;
    lastAMSDiscoveryAttempt = 0;
    mediaScreenActive = false;
    mediaTrackChangedPending = false;
    mediaTrackChangeTime = 0;

    lastNotificationTime = 0;
    notificationContentStage = false;

    // Se houver conexao atual, derruba o cliente.
    if (pClient != nullptr && pClient->isConnected())
    {
        pClient->disconnect();
    }

    pClient = nullptr;
    pANCSService = nullptr;
    pNotificationSource = nullptr;
    pControlPoint = nullptr;
    pDataSource = nullptr;
    pAMSService = nullptr;
    pAMSEntityUpdate = nullptr;
    pAMSEntityAttribute = nullptr;

    // Volta a anunciar o Meteor para permitir nova conexao.
    if (pServerGlobal != nullptr)
    {
        NimBLEAdvertising* advertising = pServerGlobal->getAdvertising();
        advertising->stop();
        delay(100);

        if (advertising->start())
        {
            bluetoothDisplayState = 0;
            Serial.println("Bluetooth reiniciado - aguardando conexao.");
        }
        else
        {
            bluetoothDisplayState = 2;
            Serial.println("ERRO ao reiniciar advertising Bluetooth.");
        }
    }

    drawDefaultLogo();
}

// ============================================================
// SETUP
// ============================================================

void setup()
{
    startIdleBlankMode();
    Serial.begin(
        115200
    );

    randomSeed((uint32_t)esp_random());

    pinMode(BUTTON_1_PIN, INPUT_PULLUP);


    delay(
        1000
    );


    Serial.println();

    Serial.println(
        "================================"
    );


    Serial.println(
        "           MOTONAV"
    );


    Serial.println(
        "================================"
    );
 // TFT

    tft.init();


    tft.setRotation(
        3
    );


    tft.fillScreen(
        TFT_BLACK
    );

    // LittleFS contem os GIFs de idle.
    if (!LittleFS.begin(false))
    {
        Serial.println("ERRO: LittleFS nao montado. GIFs de idle indisponiveis.");
    }
    else
    {
        Serial.println("LittleFS montado.");
    }

 // LOGO DESCONNECTED

    drawDisconnectedLogo();


    delay(
        2000
    );
 // BLE

    NimBLEDevice::init(
        "Meteor 350cc"
    );
 // SEGURANÇA

    NimBLEDevice::setSecurityAuth(
        true,
        true,
        true
    );


    NimBLEDevice::setSecurityIOCap(
        BLE_HS_IO_NO_INPUT_OUTPUT
    );
 // SERVER

    pServerGlobal =
        NimBLEDevice::createServer();

    NimBLEServer* pServer = pServerGlobal;

    // --------------------------------------------------------
    // SERVICO BLE LOCAL DO METEOR 350CC
    // Identifica o ESP32 como periferico BLE proprio.
    // ANCS e AMS continuam sendo usados como cliente.
    // --------------------------------------------------------
    pMeteorService = pServer->createService(METEOR_SERVICE_UUID);

    pMeteorStatusCharacteristic = pMeteorService->createCharacteristic(
        METEOR_STATUS_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE
    );

    pMeteorStatusCharacteristic->setValue("Meteor 350cc");


    pServer->setCallbacks(
        new ServerCallbacks()
    );
 // ADVERTISING

    NimBLEAdvertising* advertising =
        pServer->getAdvertising();


    NimBLEAdvertisementData advertisementData;


    advertisementData.setFlags(
        0x06
    );


    advertisementData.setName(
        "Meteor 350cc"
    );


    advertisementData.addServiceUUID(
        METEOR_SERVICE_UUID
    );


    advertising->setAdvertisementData(
        advertisementData
    );
 // INICIA ADVERTISING

    if (
        advertising->start()
    )
    {
        bluetoothDisplayState = 0;
        drawDefaultLogo();

        Serial.println(
            "Meteor 350cc: advertising ativo..."
        );
    }
    else
    {
        bluetoothDisplayState = 2;
        drawDefaultLogo();

        Serial.println(
            "ERRO: advertising."
        );
    }
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
    // BOTAO 1: reinicia somente a conexao Bluetooth.
    static bool button1WasPressed = false;
    static unsigned long button1LastAction = 0;

    bool button1Pressed = (digitalRead(BUTTON_1_PIN) == LOW);

    if (button1Pressed && !button1WasPressed &&
        millis() - button1LastAction > 500)
    {
        button1LastAction = millis();
        restartBluetoothConnection();
    }

    button1WasPressed = button1Pressed;

    // DESCOBERTA ANCS

    if (
        ancsDiscoveryRequested &&
        bleConnected &&
        pClient != nullptr &&
        pClient->isConnected()
    )
    {
        ancsDiscoveryRequested =
            false;


        if (
            !setupANCS()
        )
        {
            Serial.println(
                "Falha ao iniciar ANCS."
            );


            delay(
                1000
            );


            ancsDiscoveryRequested =
                true;
        }
    }
 // DESCOBERTA / RETENTATIVA AMS

    // O iOS pode publicar o AMS apenas quando um player de mídia
    // está disponível. Por isso tentamos novamente periodicamente.
    if (
        bleConnected &&
        ancsReady &&
        !amsReady &&
        pClient != nullptr &&
        pClient->isConnected() &&
        millis() - lastAMSDiscoveryAttempt >= 3000
    )
    {
        lastAMSDiscoveryAttempt = millis();
        setupAMS();
    }
 // NOVA NOTIFICAÇÃO

    if (
        requestAttributesPending &&
        ancsReady
    )
    {
        requestAttributesPending =
            false;


        requestNotificationAttributes();
    }
 // DATA SOURCE

    if (
        dataBufferReady
    )
    {
        processDataSource();
    }
 // NOTIFICAÇÃO COMUM EXPIRADA

    if (
        bleConnected &&
        lastNotificationTime > 0 &&
        millis() - lastNotificationTime >=
        NORMAL_NOTIFICATION_TIMEOUT
    )
    {
        lastNotificationTime =
            0;


        updateDisplay();
    }
 // MARQUEE DE MÍDIA

    updateMediaMarquee();
    updateNotificationMarquee();
 // DISPLAY

    if (
        displayUpdatePending
    )
    {
        displayUpdatePending =
            false;


        updateDisplay();
    }

    // ANIMACAO DE IDLE
    // So executa quando nao ha notificacao nem musica ativa.
    if (idleGifScreenActive &&
        lastNotificationTime == 0 &&
        !mediaScreenActive)
    {
        drawIdleGifFrame();
    }
 // YIELD

    delay(
        5
    );
}