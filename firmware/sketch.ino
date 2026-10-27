#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

struct SensorReadings {
  bool presence;
  int luminosity;
  float currentAmps;
};

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

const char* FIREBASE_API_KEY = "SUA_FIREBASE_API_KEY";
const char* FIREBASE_DATABASE_URL = "https://SEU-PROJETO-default-rtdb.firebaseio.com";

const char* DEVICE_EMAIL = "EMAIL_DA_CONTA_DO_DISPOSITIVO";
const char* DEVICE_PASSWORD = "SENHA_DA_CONTA_DO_DISPOSITIVO";

#define PIR_PIN 27
#define LDR_PIN 34
#define ACS712_PIN 36
#define RELAY_PIN 26

const int HTTP_TIMEOUT_MS = 60000;
const int HTTP_CONNECT_TIMEOUT_MS = 60000;
const unsigned long COMMAND_INTERVAL_MS = 3000;
const unsigned long TOKEN_REFRESH_INTERVAL_MS = 50UL * 60UL * 1000UL;
const unsigned long SENSOR_READ_INTERVAL_MS = 1000;
const unsigned long FIREBASE_UPLOAD_INTERVAL_MS = 10000;
const unsigned long ENERGY_SAVE_INTERVAL_MS = 10000;
const unsigned long HISTORY_RETRY_INTERVAL_MS = 10000;

unsigned long lastFirebaseUploadMs = 0;
unsigned long lastSensorReadMs = 0;
unsigned long lastTokenRefreshMs = 0;
unsigned long lastCommandCheckMs = 0;
unsigned long lastEnergyUpdateMs = 0;
unsigned long lastEnergySaveMs = 0;
unsigned long lastHistoryRetryMs = 0;
unsigned long sessionStartedAtMs = 0;

bool authenticated = false;
bool actualRelayState = false;
bool requestedRelayState = false;
bool previousWiFiState = false;
bool previousPresenceState = false;
bool historyClosePending = false;
int previousLuminosity = -1;
float previousCurrentAmps = -1.0f;
float sessionEnergyWh = 0.0f;

const float ADC_REFERENCE_V = 3.3f;
const float ADC_MAX_VALUE = 4095.0f;
const float ACS712_ZERO_V = 1.65f;
const float ACS712_SENSITIVITY_V = 0.100f;

String idToken = "";
String refreshToken = "";
String historySessionId = "";

void connectToWiFi() {
  Serial.print("[Wi-Fi] Conectando");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("[Wi-Fi] Conectado!");

  Serial.print("[Wi-Fi] IP: ");
  Serial.println(WiFi.localIP());
}

String createAuthenticationPayload() {
  JsonDocument requestBody;

  requestBody["email"] = DEVICE_EMAIL;
  requestBody["password"] = DEVICE_PASSWORD;

  String payload;
  serializeJson(requestBody, payload);

  return payload;
}

bool signInToFirebase() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Firebase] Wi-Fi desconectado");
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;
  String requestUrl = String(FIREBASE_AUTH_URL);

  Serial.println("[Firebase] Autenticando...");

  if (!http.begin(secureClient, requestUrl)) {
    Serial.println("[Firebase] Falha ao iniciar a requisicao");
    return false;
  }

  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setReuse(false);

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Connection", "close");

  String requestBody = createAuthenticationPayload();
  int httpCode = http.POST(requestBody);

  if (httpCode < 0) {
    Serial.printf("[Firebase] Erro de transporte: %s\n", http.errorToString(httpCode).c_str());
    http.end();
    return false;
  }

  String responseBody = http.getString();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[Firebase] Falha no login. HTTP: %d\n", httpCode);
    Serial.println(responseBody);
    http.end();
    return false;
  }

  http.end();

  JsonDocument responseJson;
  DeserializationError jsonError = deserializeJson(responseJson, responseBody);

  if (jsonError) {
    Serial.printf("[Firebase] Erro no JSON: %s\n", jsonError.c_str());
    return false;
  }

  idToken = responseJson["idToken"].as<String>();
  refreshToken = responseJson["refreshToken"].as<String>();

  lastTokenRefreshMs = millis();

  if (idToken.isEmpty() || refreshToken.isEmpty()) {
    Serial.println("[Firebase] Tokens ausentes na resposta");
    return false;
  }

  Serial.println("[Firebase] Autenticado!");
  return true;
}

bool refreshFirebaseToken() {
  if (WiFi.status() != WL_CONNECTED || refreshToken.isEmpty()) {
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;

  String requestUrl = String(FIREBASE_AUTH_URL);
  requestUrl.replace("/login", "/refresh");

  if (!http.begin(secureClient, requestUrl)) {
    return false;
  }

  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  JsonDocument requestBody;
  requestBody["refreshToken"] = refreshToken;

  String payload;
  serializeJson(requestBody, payload);

  int httpCode = http.POST(payload);
  String responseBody = http.getString();

  http.end();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf(
      "[Firebase] Falha ao renovar token. HTTP: %d\n",
      httpCode
    );
    return false;
  }

  JsonDocument responseJson;
  DeserializationError error =
      deserializeJson(responseJson, responseBody);

  if (error) {
    Serial.println("[Firebase] Resposta de renovacao invalida.");
    return false;
  }

  String newIdToken = responseJson["idToken"].as<String>();
  String newRefreshToken = responseJson["refreshToken"].as<String>();

  if (newIdToken.isEmpty() || newRefreshToken.isEmpty()) {
    Serial.println("[Firebase] Tokens ausentes na resposta.");
    return false;
  }

  idToken = newIdToken;
  refreshToken = newRefreshToken;
  lastTokenRefreshMs = millis();

  return true;
}

bool readRequestedState(bool& value) {
  if (WiFi.status() != WL_CONNECTED || idToken.isEmpty()) {
    Serial.println("[Firebase] Sem conexao ou token");
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;

  String requestUrl = String(FIREBASE_DATABASE_URL) + "/control/requestedState.json?auth=" + idToken;

  if (!http.begin(secureClient, requestUrl)) {
    Serial.println("[Firebase] Falha ao iniciar leitura");
    return false;
  }

  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[Firebase] Erro na leitura. HTTP: %d\n", httpCode);

    if (httpCode > 0) {
      Serial.println(http.getString());
    }

    http.end();
    return false;
  }

  String responseBody = http.getString();
  http.end();

  JsonDocument responseJson;
  DeserializationError error = deserializeJson(responseJson, responseBody);

  if (error || !responseJson.is<bool>()) {
    Serial.println("[Firebase] Comando ausente ou invalido");
    return false;
  }

  value = responseJson.as<bool>();

  return true;
}

bool updateDeviceConnected(bool connected) {
  if (WiFi.status() != WL_CONNECTED || idToken.isEmpty()) {
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;

  String requestUrl = String(FIREBASE_DATABASE_URL) + "/control/deviceConnected.json?auth=" + idToken;

  if (!http.begin(secureClient, requestUrl)) {
    return false;
  }

  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  String payload = connected ? "true" : "false";

  int httpCode = http.PUT(payload);

  http.end();

  if (httpCode < 200 || httpCode >= 300) {
    Serial.printf("[Firebase] Falha ao atualizar conexao. HTTP: %d\n", httpCode);
    return false;
  }

  return true;
}

bool updateRelayState() {
  if (WiFi.status() != WL_CONNECTED || idToken.isEmpty()) {
    Serial.println("[Firebase] Sem conexao ou token");
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;

  String requestUrl = String(FIREBASE_DATABASE_URL) + "/control/relayState.json?auth=" + idToken;

  if (!http.begin(secureClient, requestUrl)) {
    Serial.println("[Firebase] Falha ao iniciar atualizacao");
    return false;
  }

  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  String payload = actualRelayState ? "true" : "false";

  int httpCode = http.PUT(payload);

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[Firebase] Erro ao atualizar estado. HTTP: %d\n", httpCode);

    if (httpCode > 0) {
      Serial.println(http.getString());
    }

    http.end();
    return false;
  }

  http.end();

  return true;
}

void applyRelayCommand() {
  if (requestedRelayState == actualRelayState) {
    return;
  }

  actualRelayState = requestedRelayState;

  digitalWrite(RELAY_PIN, actualRelayState ? HIGH : LOW);

  updateRelayState();

  if (actualRelayState) {
    if (createHistorySession()) {
      Serial.println("[History] Sessao iniciada");
    }
  } else {
    historyClosePending = !historySessionId.isEmpty();
  
    if (historyClosePending && closeHistorySession()) {
      historyClosePending = false;
  
      Serial.println("[History] Sessao encerrada");
    }
  }

  Serial.printf("[SICE] Rele: %s\n", actualRelayState ? "LIGADO" : "DESLIGADO");
}

bool readPresence() {
  return digitalRead(PIR_PIN) == HIGH;
}

int readLuminosity() {
  return analogRead(LDR_PIN);
}

int readCurrentRaw() {
  return analogRead(ACS712_PIN);
}

float readCurrentAmps() {
  int rawValue = readCurrentRaw();

  float voltage = rawValue * (ADC_REFERENCE_V / ADC_MAX_VALUE);

  float current = (voltage - ACS712_ZERO_V) / ACS712_SENSITIVITY_V;

  return fabsf(current);
}

float readCurrentRMS() {
  const int SAMPLE_COUNT = 500;

  float sumSquares = 0.0f;

  for (int i = 0; i < SAMPLE_COUNT; i++) {
    float voltage = readCurrentRaw() * (ADC_REFERENCE_V / ADC_MAX_VALUE);

    float current = (voltage - ACS712_ZERO_V) / ACS712_SENSITIVITY_V;

    sumSquares += current * current;

    delayMicroseconds(200);
  }

  return sqrtf(sumSquares / SAMPLE_COUNT);
}

SensorReadings readSensors() {
  SensorReadings readings;

  readings.presence = readPresence();
  readings.luminosity = readLuminosity();
  readings.currentAmps = readCurrentRMS();

  return readings;
}

bool sendSensorReadings(const SensorReadings& readings) {
  if (WiFi.status() != WL_CONNECTED || idToken.isEmpty()) {
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;

  String requestUrl = String(FIREBASE_DATABASE_URL) + "/readings.json?auth=" + idToken;

  if (!http.begin(secureClient, requestUrl)) {
    return false;
  }

  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  JsonDocument payloadJson;

  payloadJson["currentAmps"] = readings.currentAmps;
  payloadJson["estimatedPowerW"] = readings.currentAmps * 127.0f;
  payloadJson["lightLevel"] = readings.luminosity;
  payloadJson["presenceDetected"] = readings.presence;
  payloadJson["measuredAt"][".sv"] = "timestamp";

  String payload;
  serializeJson(payloadJson, payload);

  int httpCode = http.POST(payload);

  String responseBody = "";
  
  if (httpCode > 0) {
    responseBody = http.getString();
  }
  
  http.end();
  
  if (httpCode < 200 || httpCode >= 300) {
    Serial.printf("[Firebase] Falha ao enviar leituras. HTTP: %d\n", httpCode);
  
    if (!responseBody.isEmpty()) {
      Serial.println(responseBody);
    }
  
    return false;
  }
  
  return true;
}

bool createHistorySession() {
  if (WiFi.status() != WL_CONNECTED || idToken.isEmpty()) {
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;

  String requestUrl = String(FIREBASE_DATABASE_URL) + "/history.json?auth=" + idToken;

  if (!http.begin(secureClient, requestUrl)) {
    return false;
  }

  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  JsonDocument payloadJson;

  payloadJson["event"] = "ligado";
  payloadJson["startedAt"][".sv"] = "timestamp";
  payloadJson["energyWh"] = 0.0f;

  String payload;
  serializeJson(payloadJson, payload);

  int httpCode = http.POST(payload);
  String responseBody = httpCode > 0 ? http.getString() : "";

  http.end();

  if (httpCode < 200 || httpCode >= 300) {
    Serial.printf("[Firebase] Falha ao criar sessao. HTTP: %d\n", httpCode);

    if (!responseBody.isEmpty()) {
      Serial.println(responseBody);
    }

    return false;
  }

  JsonDocument responseJson;
  DeserializationError error = deserializeJson(responseJson, responseBody);

  if (error || !responseJson["name"].is<const char*>()) {
    Serial.println("[Firebase] ID da sessao ausente.");
    return false;
  }

  historySessionId = responseJson["name"].as<String>();

  sessionStartedAtMs = millis();
  lastEnergyUpdateMs = sessionStartedAtMs;
  sessionEnergyWh = 0.0f;
  lastEnergySaveMs = sessionStartedAtMs;

  return true;
}

bool closeHistorySession() {
  if (
    WiFi.status() != WL_CONNECTED ||
    idToken.isEmpty() ||
    historySessionId.isEmpty()
  ) {
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;

  String requestUrl = String(FIREBASE_DATABASE_URL) + "/history/" + historySessionId + ".json?auth=" + idToken;

  if (!http.begin(secureClient, requestUrl)) {
    return false;
  }

  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  JsonDocument payloadJson;

  payloadJson["event"] = "desligado";
  payloadJson["endedAt"][".sv"] = "timestamp";
  payloadJson["energyWh"] = sessionEnergyWh;

  String payload;
  serializeJson(payloadJson, payload);

  int httpCode = http.PATCH(payload);
  String responseBody = httpCode > 0 ? http.getString() : "";

  http.end();

  if (httpCode < 200 || httpCode >= 300) {
    Serial.printf("[Firebase] Falha ao fechar sessao. HTTP: %d\n", httpCode);

    if (!responseBody.isEmpty()) {
      Serial.println(responseBody);
    }

    return false;
  }

  historySessionId = "";

  return true;
}

void updateSessionEnergy(float currentAmps) {
  if (!actualRelayState || historySessionId.isEmpty()) {
    return;
  }

  unsigned long currentTimeMs = millis();
  unsigned long elapsedMs = currentTimeMs - lastEnergyUpdateMs;

  float estimatedPowerW = currentAmps * 127.0f;

  sessionEnergyWh += estimatedPowerW * (elapsedMs / 3600000.0f);

  lastEnergyUpdateMs = currentTimeMs;
}

bool saveSessionEnergy() {
  if (WiFi.status() != WL_CONNECTED || idToken.isEmpty() || historySessionId.isEmpty()) {
    return false;
  }

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;

  String requestUrl = String(FIREBASE_DATABASE_URL) + "/history/" + historySessionId + "/energyWh.json?auth=" + idToken;

  if (!http.begin(secureClient, requestUrl)) {
    return false;
  }

  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");

  String payload = String(sessionEnergyWh, 6);

  int httpCode = http.PUT(payload);

  http.end();

  if (httpCode < 200 || httpCode >= 300) {
    Serial.printf("[Firebase] Falha ao salvar energia. HTTP: %d\n", httpCode);

    return false;
  }

  return true;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(ACS712_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);

  Serial.println();
  Serial.println("[SICE] Inicializando...");
  
  connectToWiFi();

  authenticated = signInToFirebase();
  previousWiFiState = (WiFi.status() == WL_CONNECTED);

  if (authenticated) {
    updateDeviceConnected(true);
    
    bool command;

    if (readRequestedState(command)) {
      requestedRelayState = command;

      Serial.printf("[Firebase] Comando: %s\n", command ? "LIGAR" : "DESLIGAR");
    }
  }
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    if (previousWiFiState) {
      updateDeviceConnected(false);
      previousWiFiState = false;
    }

    connectToWiFi();

    if (WiFi.status() == WL_CONNECTED) {
      previousWiFiState = true;

      if (authenticated && !idToken.isEmpty()) {
        updateDeviceConnected(true);
      }
    }
  }

  const unsigned long currentTimeMs = millis();

  if (currentTimeMs - lastTokenRefreshMs >= TOKEN_REFRESH_INTERVAL_MS) {
    if (refreshFirebaseToken()) {
      Serial.println("[Firebase] Token renovado!");
    }
  }

  if (currentTimeMs - lastCommandCheckMs >= COMMAND_INTERVAL_MS) {
    lastCommandCheckMs = currentTimeMs;

    bool command;

    if (readRequestedState(command)) {
      if (command != requestedRelayState) {
        requestedRelayState = command;

        Serial.printf("[Firebase] Comando: %s\n", command ? "LIGAR" : "DESLIGAR");
      }
    }
  }

  applyRelayCommand();

  if (historyClosePending && currentTimeMs - lastHistoryRetryMs >= HISTORY_RETRY_INTERVAL_MS) {
    lastHistoryRetryMs = currentTimeMs;

    if (closeHistorySession()) {
      historyClosePending = false;

      Serial.println("[History] Sessao encerrada");
    }
  }

  if (currentTimeMs - lastSensorReadMs >= SENSOR_READ_INTERVAL_MS) {
    lastSensorReadMs = currentTimeMs;

    SensorReadings readings = readSensors();
    
    updateSessionEnergy(readings.currentAmps);

    if (actualRelayState && !historySessionId.isEmpty() && currentTimeMs - lastEnergySaveMs >= ENERGY_SAVE_INTERVAL_MS) {
      if (saveSessionEnergy()) {
        lastEnergySaveMs = currentTimeMs;
      }
    }

    if (currentTimeMs - lastFirebaseUploadMs >= FIREBASE_UPLOAD_INTERVAL_MS) {
      if (sendSensorReadings(readings)) {
        lastFirebaseUploadMs = currentTimeMs;
        Serial.println("[Firebase] Leituras enviadas");
      }
    }

    if (readings.presence != previousPresenceState) {
      previousPresenceState = readings.presence;

      Serial.printf("[Sensor] Presenca: %s\n", readings.presence ? "DETECTADA" : "NAO DETECTADA");
    }

    if (readings.luminosity != previousLuminosity) {
      previousLuminosity = readings.luminosity;

      Serial.printf("[Sensor] Luminosidade ADC: %d\n", readings.luminosity);
    }

    if (previousCurrentAmps < 0.0f || fabsf(readings.currentAmps - previousCurrentAmps) >= 0.1f) {
      previousCurrentAmps = readings.currentAmps;

      Serial.printf("[Sensor] Corrente RMS: %.2f A\n", readings.currentAmps);
    }
  }

  delay(10);
}
