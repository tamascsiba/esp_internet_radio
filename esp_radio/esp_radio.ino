#include <WiFi.h>
#include <Audio.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Fonts/FreeSans12pt7b.h>

// ======== I2S audio output (MAX98357A) ========
#define I2S_DOUT  14  // DIN -> GPIO14
#define I2S_BCLK  12  // BCLK -> GPIO12
#define I2S_LRC   13  // LRC  -> GPIO13

// ======== Analog volume pin (ADC1, safe pin) ========
#define VOLUME_PIN 4  // GPIO4 (ADC1_CH3)

// ======== Rotary Encoder pins ========
const uint8_t CLK = 6;   // rotary encoder A
const uint8_t DT  = 7;   // rotary encoder B

// ======== OLED Display (SSD1306 I2C) ========
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define I2C_SDA 18
#define I2C_SCL 17

// ======== Objects ========
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Audio audio;

// ======== WiFi credentials ========
const char* ssid = "Csiba_extender";
const char* password = "Csiba123456789";

// ======== Radio stations ========
const char* stations[] = {
    "https://icast.connectmedia.hu/5223/live.mp3",
    "https://icast.connectmedia.hu/5001/live.mp3",
    "https://icast.connectmedia.hu/4738/mr2.mp3",
    "https://slagerfm.netregator.hu:7813/slagerfm128.mp3",
    "https://stream.42netmedia.com:8443/sc_gyor1",
    "https://stream.bauermedia.sk/128.mp3",
    "https://stream.funradio.sk:8000/fun128.mp3",
    "https://stream.bauermedia.sk/melody-hi.mp3",
    "https://stream.bauermedia.sk/europa2.mp3",
    "https://stream.bauermedia.sk/rock-hi.mp3",
    "https://stream.radiovlna.sk/vlna-hi.mp3"
};

const char* stationNames[] = {
    "Radio 1",
    "Retro Radio",
    "Petofi Radio",
    "Slager FM",
    "Gyor Plusz Radio",
    "Radio Expres",
    "Fun Radio",
    "Radio Jemne",
    "Europa 2",
    "Radio Rock",
    "Radio Vlna"
};

const int NUM_STATIONS = sizeof(stations) / sizeof(stations[0]);
int currentStation = 0;
char streamTitle[64] = "";

// ======== Volume control variables ========
const int SAMPLES = 5;
int volumeReadings[SAMPLES];
int readIndex = 0;
int total = 0;
int average = 0;
unsigned long lastVolumeCheck = 0;
const unsigned long VOLUME_CHECK_INTERVAL = 50;

bool isWiFiConnected = false;
bool isDisplayInitialized = false;
bool isAudioInitialized = false;

// ======== Rotary Encoder state-machine variables ========
int16_t inputDelta = 0;
bool printFlag = false;
uint8_t encoderState = 0;

// ======== SETUP ========
void setup() {
    Serial.begin(115200);
    pinMode(VOLUME_PIN, INPUT);
    pinMode(CLK, INPUT_PULLUP);
    pinMode(DT, INPUT_PULLUP);

    // I2C setup (OLED)
    Wire.begin(I2C_SDA, I2C_SCL);

    for (int i = 0; i < SAMPLES; i++) volumeReadings[i] = 0;

    initializeDisplay();
    connectToWiFi();
    initializeAudio();
}

// ======== LOOP ========
void loop() {
    audio.loop();
    readEncoder();
    processEncoder();
    checkVolumeControl();
}

// ======== DISPLAY ========
void initializeDisplay() {
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) return;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,0);
    display.println("Initializing...");
    display.display();
    isDisplayInitialized = true;
}

// ======== WIFI ========
void connectToWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ WiFi Connected!");
        isWiFiConnected = true;
        if (isDisplayInitialized) {
            display.clearDisplay();
            display.setCursor(0,0);
            display.println("WiFi Connected!");
            display.display();
        }
    }
}

// ======== AUDIO ========
void initializeAudio() {
    audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
    connectToStation(currentStation);
    isAudioInitialized = true;
}

// ======== ROTARY ENCODER FUNCTIONS (state-machine) ========
void readEncoder() {
    bool CLKstate = digitalRead(CLK);
    bool DTstate = digitalRead(DT);

    switch (encoderState) {
        case 0:
            if (!CLKstate) encoderState = 1;
            else if (!DTstate) encoderState = 4;
            break;
        case 1:
            if (!DTstate) encoderState = 2;
            break;
        case 2:
            if (CLKstate) encoderState = 3;
            break;
        case 3:
            if (CLKstate && DTstate) {
                encoderState = 0;
                ++inputDelta;
                printFlag = true;
            }
            break;
        case 4:
            if (!CLKstate) encoderState = 5;
            break;
        case 5:
            if (DTstate) encoderState = 6;
            break;
        case 6:
            if (CLKstate && DTstate) {
                encoderState = 0;
                --inputDelta;
                printFlag = true;
            }
            break;
    }
}

void processEncoder() {
    if (printFlag) {
        printFlag = false;

        currentStation += inputDelta;
        if (currentStation < 0) currentStation = NUM_STATIONS - 1;
        else if (currentStation >= NUM_STATIONS) currentStation = 0;

        connectToStation(currentStation);
        inputDelta = 0;
    }
}

// ======== VOLUME CONTROL ========
void checkVolumeControl() {
    unsigned long currentMillis = millis();
    if (currentMillis - lastVolumeCheck >= VOLUME_CHECK_INTERVAL) {
        lastVolumeCheck = currentMillis;
        total -= volumeReadings[readIndex];
        volumeReadings[readIndex] = analogRead(VOLUME_PIN);
        total += volumeReadings[readIndex];
        readIndex = (readIndex + 1) % SAMPLES;

        average = total / SAMPLES;
        int volume = map(average, 0, 4095, 0, 21);
        static int lastVolume = -1;
        if (volume != lastVolume) {
            audio.setVolume(volume);
            lastVolume = volume;
            updateDisplay();
        }
    }
}

// ======== DISPLAY UPDATE ========
void updateDisplay() {
    if (!isDisplayInitialized) return;
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0,0);
    display.println(stationNames[currentStation]);
    display.println();
    display.println(streamTitle);
    display.display();
}

// ======== CONNECT TO STATION ========
void connectToStation(int stationIndex) {
    audio.stopSong();
    Serial.print("🎵 Connecting to: ");
    Serial.println(stationNames[stationIndex]);
    audio.connecttohost(stations[stationIndex]);
    updateDisplay();
}

// ======== STREAM TITLE CALLBACK ========
void audio_showstreamtitle(const char *info) {
    Serial.print("Stream title: ");
    Serial.println(info);
    strncpy(streamTitle, info, sizeof(streamTitle) - 1);
    streamTitle[sizeof(streamTitle) - 1] = '\0';
    updateDisplay();
}
