// Archivo: ControlEsp32.ino
// Descripción: Panel de control para ESP32 con interfaz web.
//              Notifica desconexiones WiFi en la UI, limpia EEPROM solo en primera carga o corrupción,
//              usa CRC para detectar datos corruptos. Botones con iconos Unicode.
// Fecha: 22 de mayo de 2025

#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>

// === Configuración de pines ===
#define NUM_DIGITAL_OUT 3
const int DIGITAL_OUT[NUM_DIGITAL_OUT] = {5, 23, 18}; // GPIO5, GPIO23, GPIO18
#define NUM_DIGITAL_IN 5
const int DIGITAL_IN[NUM_DIGITAL_IN] = {13, 17, 26, 14, 27}; // GPIO13, GPIO17, GPIO26, GPIO14, GPIO27

// === Configuración de WiFi ===
struct Config {
  char ssid[32] = "Personal-075";
  char password[32] = "FXwPVTAt8a";
  char ap_ssid[32] = "ESP32_AP";
  char ap_password[32] = "12345678";
  bool forceAPMode = false;
  uint32_t crc; // Para verificación de integridad
};

Config config;
WebServer server(80);
bool wifiConnected = false; // Estado de conexión WiFi

// === Variables globales para sensores ===
float temperatures[4] = {0, 0, 0, 0};
float weights[4] = {0, 0, 0, 0};
bool digitalInputs[NUM_DIGITAL_IN];
bool digitalOutputs[NUM_DIGITAL_OUT];
bool sdInitialized = false;

// === Funciones de utilidad ===
// Función CRC-32 para verificar datos
uint32_t calculateCRC32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
    }
  }
  return ~crc;
}

void saveConfig() {
  // Calcular CRC de los datos (excluyendo el campo crc)
  config.crc = calculateCRC32((uint8_t*)&config, sizeof(Config) - sizeof(config.crc));
  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println("Configuración guardada en EEPROM con CRC: " + String(config.crc, HEX));
}

bool loadConfig() {
  EEPROM.get(0, config);
  uint32_t storedCRC = config.crc;
  config.crc = 0; // Excluir el CRC almacenado del cálculo
  uint32_t calculatedCRC = calculateCRC32((uint8_t*)&config, sizeof(Config) - sizeof(config.crc));
  Serial.println("Configuración cargada desde EEPROM:");
  Serial.println("SSID: " + String(config.ssid));
  Serial.println("Clave: " + String(config.password));
  Serial.println("AP SSID: " + String(config.ap_ssid));
  Serial.println("AP Clave: " + String(config.ap_password));
  Serial.println("Forzar Modo AP: " + String(config.forceAPMode));
  Serial.println("CRC almacenado: " + String(storedCRC, HEX) + ", Calculado: " + String(calculatedCRC, HEX));
  return storedCRC == calculatedCRC;
}

void clearEEPROM() {
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  Serial.println("EEPROM limpiada.");
  // Restablecer credenciales predeterminadas
  strncpy(config.ssid, "Personal-075", sizeof(config.ssid));
  strncpy(config.password, "FXwPVTAt8a", sizeof(config.password));
  strncpy(config.ap_ssid, "ESP32_AP", sizeof(config.ap_ssid));
  strncpy(config.ap_password, "12345678", sizeof(config.ap_password));
  config.forceAPMode = false;
  saveConfig();
}

String getBoardInfo() {
  String info = "<h5>Información de la Placa</h5>";
  info += "<p><strong>Chip:</strong> ESP32-DevKitC V4</p>";
  info += "<p><strong>Frecuencia de la CPU:</strong> " + String(ESP.getCpuFreqMHz()) + " MHz</p>";
  info += "<p><strong>Tamaño de la memoria Flash:</strong> " + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB</p>";
  return info;
}

void setupWiFi() {
  Serial.println("Configurando WiFi...");
  WiFi.disconnect(true); // Forzar desconexión completa
  WiFi.mode(WIFI_OFF); // Asegurar que el WiFi esté apagado
  delay(500); // Pausa para estabilizar

  // Validar credenciales STA
  bool validSTACredentials = strlen(config.ssid) > 0 && strlen(config.password) > 0 && config.ssid[0] != 0xFF && config.ssid[0] != '\0';
  if (!config.forceAPMode && validSTACredentials) {
    Serial.print("Intentando conectar en modo STA a SSID: ");
    Serial.println(config.ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.ssid, config.password);

    unsigned long startAttempt = millis();
    const unsigned long timeout = 15000; // 15 segundos
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < timeout) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Conectado en modo STA. IP: " + WiFi.localIP().toString());
      config.forceAPMode = false;
      wifiConnected = true;
      saveConfig();
      return;
    } else {
      Serial.print("No se pudo conectar en modo STA. Estado WiFi: ");
      switch (WiFi.status()) {
        case WL_NO_SSID_AVAIL:
          Serial.println("SSID no encontrado");
          break;
        case WL_CONNECT_FAILED:
          Serial.println("Fallo de conexión (credenciales incorrectas?)");
          break;
        case WL_DISCONNECTED:
          Serial.println("Desconectado");
          break;
        default:
          Serial.println("Error desconocido, código: " + String(WiFi.status()));
          break;
      }
      wifiConnected = false;
    }
  } else {
    Serial.println("Credenciales STA inválidas o modo AP forzado.");
    Serial.println("SSID válido: " + String(validSTACredentials ? "Sí" : "No"));
    Serial.println("Forzar modo AP: " + String(config.forceAPMode ? "Sí" : "No"));
    wifiConnected = false;
  }

  // Cambiar a modo AP
  Serial.println("Configurando modo AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(config.ap_ssid, config.ap_password);
  delay(500); // Esperar a que el AP se inicie
  Serial.println("Modo AP activado. IP: " + WiFi.softAPIP().toString());
  config.forceAPMode = true;
  wifiConnected = false;
  saveConfig();
}

// === Handlers del servidor web ===
void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html lang="es">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate">
    <meta http-equiv="Pragma" content="no-cache">
    <meta http-equiv="Expires" content="0">
    <title>ESP32 Panel de Control</title>
    <style>
      body {
        font-family: Arial, sans-serif;
        background-color: #f0f2f5;
        margin: 0;
        padding: 0;
      }
      .navbar {
        background-color: #343a40;
        padding: 15px 0;
        position: fixed;
        width: 100%;
        top: 0;
        z-index: 1000;
      }
      .navbar a {
        color: white;
        padding: 14px 20px;
        text-decoration: none;
        display: inline-block;
      }
      .navbar a:hover {
        background-color: #495057;
      }
      .container {
        max-width: 1200px;
        margin: 80px auto 20px auto;
        padding: 20px;
      }
      .card {
        background: #fff;
        padding: 20px;
        margin-bottom: 20px;
        border-radius: 8px;
        box-shadow: 0 2px 4px rgba(0,0,0,0.1);
      }
      h5 {
        margin: 0 0 10px;
        color: #343a40;
      }
      table {
        width: 100%;
        border-collapse: collapse;
        margin-bottom: 20px;
      }
      th, td {
        border: 1px solid #ddd;
        padding: 8px;
        text-align: left;
      }
      th {
        background: #f8f9fa;
      }
      .btn {
        padding: 8px 16px;
        border: none;
        border-radius: 4px;
        cursor: pointer;
        margin: 5px;
        transition: background-color 0.3s;
        display: inline-flex;
        align-items: center;
        justify-content: center;
        font-size: 16px;
      }
      .btn-primary {
        background: #007bff;
        color: #fff;
      }
      .btn-primary:hover {
        background: #0056b3;
      }
      .btn-secondary {
        background: #6c757d;
        color: #fff;
      }
      .btn-secondary:hover {
        background: #5a6268;
      }
      .status-on { color: #28a745; }
      .status-off { color: #dc3545; }
      .form-group {
        margin-bottom: 15px;
        display: flex;
        align-items: center;
        gap: 10px;
        flex-wrap: wrap;
      }
      .form-row {
        display: flex;
        flex-wrap: wrap;
        gap: 20px;
        align-items: flex-start;
      }
      .form-row .form-group {
        flex: 1;
        min-width: 300px;
      }
      input[type="text"], input[type="password"] {
        padding: 8px;
        margin: 5px 0;
        width: 100%;
        max-width: 300px;
        border: 1px solid #ddd;
        border-radius: 4px;
      }
      .footer {
        background-color: #343a40;
        color: white;
        text-align: center;
        padding: 10px 0;
        position: fixed;
        bottom: 0;
        width: 100%;
      }
      .action-buttons {
        display: inline-flex;
        gap: 5px;
      }
      .password-group {
        display: flex;
        align-items: center;
        gap: 5px;
        max-width: 350px;
      }
    </style>
  </head>
  <body>
    <div class="navbar">
      <a href="#system-status">Estado del Sistema</a>
      <a href="#network-config">Configuración de Red</a>
      <a href="#data-management">Gestión de Datos</a>
    </div>

    <div class="container">
      <div class="card" id="system-status">
        )rawliteral";
  html += getBoardInfo();
  html += R"rawliteral(
        <h5>Estado del Sistema</h5>
        <p><strong>SD Card:</strong> <span id="sdStatus">Cargando...</span></p>
        <p><strong>WiFi:</strong> <span id="wifiStatus">Cargando...</span></p>
        <p><strong>Modo:</strong> )rawliteral";
  html += config.forceAPMode ? "AP" : "STA";
  html += R"rawliteral( <button class="btn btn-primary" onclick="toggleMode()" title="Alternar modo WiFi">📡</button></p>
        <h5>Datos Actuales</h5>
        <table>
          <tr>
            <th>Sensor</th>
            <th>Temperatura (°C)</th>
            <th>Acción Temp</th>
            <th>Peso (units)</th>
            <th>Acción Peso</th>
          </tr>
          )rawliteral";
  for (int i = 0; i < 4; i++) {
    html += "<tr><td>Sensor " + String(i + 1) + "</td><td><span id='temp" + String(i) + "'>...</span></td><td><button class='btn btn-secondary' onclick='updateSensors()' title='Actualizar'>🔄</button></td><td><span id='weight" + String(i) + "'>...</span></td><td class='action-buttons'><button class='btn btn-secondary' onclick='updateSensors()' title='Actualizar'>🔄</button><button class='btn btn-secondary' onclick='tareSensor(" + String(i) + ")' title='Tara'>⚖️</button></td></tr>";
  }
  html += R"rawliteral(
        </table>
        <table>
          <tr>
            <th>Pin</th>
            <th>Entrada Digital</th>
            <th>Acción Entrada</th>
            <th>Salida Digital</th>
            <th>Acción Salida</th>
          </tr>
          )rawliteral";
  for (int i = 0; i < NUM_DIGITAL_IN; i++) {
    int doutIndex = i < NUM_DIGITAL_OUT ? i : -1;
    html += "<tr><td>Pin " + String(i + 1) + "</td><td><span id='din" + String(i) + "'>...</span></td><td><button class='btn btn-secondary' onclick='updateSensors()' title='Actualizar'>🔄</button></td>";
    if (doutIndex >= 0) {
      html += "<td><span id='dout" + String(doutIndex) + "'>...</span></td><td><button class='btn btn-secondary' onclick='toggleOutput(" + String(doutIndex) + ")' title='Alternar salida'>🔌</button></td>";
    } else {
      html += "<td>-</td><td>-</td>";
    }
    html += "</tr>";
  }
  html += R"rawliteral(
        </table>
      </div>
      <div class="card" id="network-config">
        <h5>Configuración de Red</h5>
        <div class="form-row">
          <div class="form-group">
            <h6>Modo STA</h6>
            <form id="staConfigForm" onsubmit="saveConfig(event, 'sta')">
              <input type="text" id="ssid" name="ssid" placeholder="SSID" value=")rawliteral";
  html += String(config.ssid);
  html += R"rawliteral(" required>
              <div class="password-group">
                <input type="password" id="password" name="password" placeholder="Contraseña" value=")rawliteral";
  html += String(config.password);
  html += R"rawliteral(" required>
                <button type="button" class="btn btn-secondary" id="togglePassword" onclick="togglePasswordVisibility('password', 'togglePassword')" title="Mostrar/Ocultar contraseña"><span id="togglePasswordIcon">👁️</span></button>
              </div>
              <div class="action-buttons">
                <button type="submit" class="btn btn-primary" title="Guardar">💾</button>
                <button type="button" class="btn btn-secondary" onclick="document.getElementById('ssid').value='';document.getElementById('password').value=''" title="Limpiar">🧹</button>
              </div>
            </form>
          </div>
          <div class="form-group">
            <h6>Modo AP</h6>
            <form id="apConfigForm" onsubmit="saveConfig(event, 'ap')">
              <input type="text" id="ap_ssid" name="ap_ssid" placeholder="SSID AP" value=")rawliteral";
  html += String(config.ap_ssid);
  html += R"rawliteral(" required>
              <div class="password-group">
                <input type="password" id="ap_password" name="ap_password" placeholder="Contraseña AP" value=")rawliteral";
  html += String(config.ap_password);
  html += R"rawliteral(" required>
                <button type="button" class="btn btn-secondary" id="toggleApPassword" onclick="togglePasswordVisibility('ap_password', 'toggleApPassword')" title="Mostrar/Ocultar contraseña"><span id="toggleApPasswordIcon">👁️</span></button>
              </div>
              <div class="action-buttons">
                <button type="submit" class="btn btn-primary" title="Guardar">💾</button>
                <button type="button" class="btn btn-secondary" onclick="document.getElementById('ap_ssid').value='';document.getElementById('ap_password').value=''" title="Limpiar">🧹</button>
              </div>
            </form>
          </div>
        </div>
      </div>
      <div class="card" id="data-management">
        <h5>Gestión de Datos</h5>
        <div class="action-buttons">
          <button class="btn btn-primary" onclick="downloadData()" title="Descargar datos">📥</button>
          <button class="btn btn-primary" onclick="viewData()" title="Ver históricos">📊</button>
        </div>
        <div id="historicalData"></div>
      </div>
    </div>

    <div class="footer">
      <p>Telemetric - Versión 1.0 - Mayo 2025 (c) Fabian Alaniz - ElectroNet</p>
    </div>

    <script>
      function togglePasswordVisibility(inputId, buttonId) {
        const input = document.getElementById(inputId);
        const icon = document.getElementById(buttonId + 'Icon');
        if (input.type === 'password') {
          input.type = 'text';
          icon.innerText = '🙈';
        } else {
          input.type = 'password';
          icon.innerText = '👁️';
        }
      }
      function updateSensors() {
        fetch('/data')
          .then(response => response.json())
          .then(data => {
            for (let i = 0; i < 4; i++) {
              document.getElementById(`temp${i}`).innerText = data.temperatures[i].toFixed(2);
              document.getElementById(`weight${i}`).innerText = data.weights[i].toFixed(2);
            }
            for (let i = 0; i < )rawliteral" + String(NUM_DIGITAL_IN) + R"rawliteral(; i++) {
              let din = document.getElementById(`din${i}`);
              din.innerText = data.digitalInputs[i] ? 'HIGH' : 'LOW';
              din.className = data.digitalInputs[i] ? 'status-on' : 'status-off';
            }
            for (let i = 0; i < )rawliteral" + String(NUM_DIGITAL_OUT) + R"rawliteral(; i++) {
              let dout = document.getElementById(`dout${i}`);
              dout.innerText = data.digitalOutputs[i] ? 'HIGH' : 'LOW';
              dout.className = data.digitalOutputs[i] ? 'status-on' : 'status-off';
            }
          })
          .catch(error => console.error('Error:', error));
      }
      function updateSDStatus() {
        fetch('/sdStatus')
          .then(response => response.json())
          .then(data => {
            let status = document.getElementById('sdStatus');
            status.innerText = data.sdInitialized ? 'Conectado' : 'No Conectado';
            status.className = data.sdInitialized ? 'status-on' : 'status-off';
          })
          .catch(error => console.error('Error:', error));
      }
      function updateWiFiStatus() {
        fetch('/wifiStatus')
          .then(response => response.json())
          .then(data => {
            let status = document.getElementById('wifiStatus');
            status.innerText = data.message;
            status.className = data.connected ? 'status-on' : 'status-off';
          })
          .catch(error => console.error('Error:', error));
      }
      function toggleOutput(index) {
        fetch(`/setOutput?index=${index}`)
          .then(response => response.text())
          .then(() => updateSensors())
          .catch(error => console.error('Error:', error));
      }
      function tareSensor(index) {
        fetch(`/tareSensor?index=${index}`)
          .then(response => response.text())
          .then(message => {
            alert(message);
            updateSensors();
          })
          .catch(error => console.error('Error:', error));
      }
      function toggleMode() {
        fetch('/toggleMode')
          .then(response => response.text())
          .then(message => {
            alert(message);
            setTimeout(() => location.reload(), 2000);
          })
          .catch(error => console.error('Error:', error));
      }
      function saveConfig(event, mode) {
        event.preventDefault();
        const form = event.target;
        const formData = new FormData(form);
        fetch('/saveConfig', {
          method: 'POST',
          body: formData
        })
          .then(response => response.text())
          .then(message => {
            alert(message);
            setTimeout(() => location.reload(), 2000);
          })
          .catch(error => console.error('Error:', error));
      }
      function downloadData() {
        window.location.href = '/downloadData';
      }
      function viewData() {
        fetch('/viewData')
          .then(response => response.json())
          .then(data => {
            const container = document.getElementById('historicalData');
            if (data.error) {
              container.innerHTML = `<p style="color:red">${data.error}</p>`;
              return;
            }
            let table = `<table><tr><th>Timestamp</th>`;
            for (let i = 1; i <= 4; i++) table += `<th>Temp ${i}</th>`;
            for (let i = 1; i <= 4; i++) table += `<th>Peso ${i}</th>`;
            for (let i = 1; i <= )rawliteral" + String(NUM_DIGITAL_IN) + R"rawliteral(; i++) table += `<th>In ${i}</th>`;
            for (let i = 1; i <= )rawliteral" + String(NUM_DIGITAL_OUT) + R"rawliteral(; i++) table += `<th>Out ${i}</th>`;
            table += `</tr>`;
            data.data.forEach(line => {
              const values = line.split(',');
              table += '<tr>';
              for (let i = 0; i < values.length; i++) {
                table += `<td>${values[i]}</td>`;
              }
              table += '</tr>';
            });
            table += '</table>';
            container.innerHTML = table;
          })
          .catch(error => console.error('Error:', error));
      }
      setInterval(updateSensors, 500);
      setInterval(updateSDStatus, 5000);
      setInterval(updateWiFiStatus, 1000); // Actualizar estado WiFi cada segundo
      updateSensors();
      updateSDStatus();
      updateWiFiStatus();
    </script>
  </body>
  </html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void handleData() {
  String json = "{";
  json += "\"temperatures\":[";
  for (int i = 0; i < 4; i++) {
    temperatures[i] = random(200, 300) / 10.0;
    json += String(temperatures[i], 2);
    if (i < 3) json += ",";
  }
  json += "],";
  json += "\"weights\":[";
  for (int i = 0; i < 4; i++) {
    weights[i] = random(100, 1000) / 10.0;
    json += String(weights[i], 2);
    if (i < 3) json += ",";
  }
  json += "],";
  json += "\"digitalInputs\":[";
  for (int i = 0; i < NUM_DIGITAL_IN; i++) {
    digitalInputs[i] = digitalRead(DIGITAL_IN[i]);
    json += digitalInputs[i] ? "true" : "false";
    if (i < NUM_DIGITAL_IN - 1) json += ",";
  }
  json += "],";
  json += "\"digitalOutputs\":[";
  for (int i = 0; i < NUM_DIGITAL_OUT; i++) {
    json += digitalOutputs[i] ? "true" : "false";
    if (i < NUM_DIGITAL_OUT - 1) json += ",";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleSDStatus() {
  String json = "{\"sdInitialized\":" + String(sdInitialized ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleWiFiStatus() {
  String json = "{";
  json += "\"connected\":" + String(wifiConnected ? "true" : "false") + ",";
  if (wifiConnected && !config.forceAPMode) {
    json += "\"message\":\"Conectado a " + String(config.ssid) + ", IP: " + WiFi.localIP().toString() + "\"";
  } else if (config.forceAPMode) {
    json += "\"message\":\"Modo AP: " + String(config.ap_ssid) + ", IP: " + WiFi.softAPIP().toString() + "\"";
  } else {
    json += "\"message\":\"Desconectado de " + String(config.ssid) + "\"";
  }
  json += "}";
  server.send(200, "application/json", json);
}

void handleSetOutput() {
  if (server.hasArg("index")) {
    int index = server.arg("index").toInt();
    if (index >= 0 && index < NUM_DIGITAL_OUT) {
      digitalOutputs[index] = !digitalOutputs[index];
      digitalWrite(DIGITAL_OUT[index], digitalOutputs[index] ? HIGH : LOW);
      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(400, "text/plain", "Índice inválido");
}

void handleTareSensor() {
  if (server.hasArg("index")) {
    int index = server.arg("index").toInt();
    if (index >= 0 && index < 4) {
      weights[index] = 0;
      server.send(200, "text/plain", "Tara realizada en sensor " + String(index + 1));
      return;
    }
  }
  server.send(400, "text/plain", "Índice inválido");
}

void handleToggleMode() {
  config.forceAPMode = !config.forceAPMode;
  saveConfig();
  Serial.println("Cambiando modo a " + String(config.forceAPMode ? "AP" : "STA"));
  server.send(200, "text/plain", "Modo cambiado. Intentando reconexión...");
  WiFi.disconnect(true);
  delay(100);
  setupWiFi();
}

void handleSaveConfig() {
  if (server.hasArg("ssid") && server.hasArg("password")) {
    strncpy(config.ssid, server.arg("ssid").c_str(), sizeof(config.ssid));
    strncpy(config.password, server.arg("password").c_str(), sizeof(config.password));
    config.forceAPMode = false;
    Serial.println("Nuevas credenciales STA guardadas: " + String(config.ssid));
  } else if (server.hasArg("ap_ssid") && server.hasArg("ap_password")) {
    strncpy(config.ap_ssid, server.arg("ap_ssid").c_str(), sizeof(config.ap_ssid));
    strncpy(config.ap_password, server.arg("ap_password").c_str(), sizeof(config.ap_password));
    config.forceAPMode = true;
    Serial.println("Nuevas credenciales AP guardadas: " + String(config.ap_ssid));
  } else {
    server.send(400, "text/plain", "Parámetros inválidos");
    return;
  }
  saveConfig();
  server.send(200, "text/plain", "Configuración guardada. Intentando reconexión...");
  WiFi.disconnect(true);
  delay(100);
  setupWiFi();
}

void handleDownloadData() {
  String data = "Timestamp,Temp1,Temp2,Temp3,Temp4,Weight1,Weight2,Weight3,Weight4";
  for (int i = 0; i < NUM_DIGITAL_IN; i++) data += ",In" + String(i + 1);
  for (int i = 0; i < NUM_DIGITAL_OUT; i++) data += ",Out" + String(i + 1);
  data += "\n";
  data += String(millis()) + ",";
  for (int i = 0; i < 4; i++) data += String(temperatures[i], 2) + ",";
  for (int i = 0; i < 4; i++) data += String(weights[i], 2) + ",";
  for (int i = 0; i < NUM_DIGITAL_IN; i++) data += String(digitalInputs[i]) + ",";
  for (int i = 0; i < NUM_DIGITAL_OUT; i++) data += String(digitalOutputs[i]) + (i < NUM_DIGITAL_OUT - 1 ? "," : "");
  server.send(200, "text/plain", data);
}

void handleViewData() {
  String json = "{\"data\":[\"";
  json += String(millis()) + ",";
  for (int i = 0; i < 4; i++) json += String(temperatures[i], 2) + ",";
  for (int i = 0; i < 4; i++) json += String(weights[i], 2) + ",";
  for (int i = 0; i < NUM_DIGITAL_IN; i++) json += String(digitalInputs[i]) + ",";
  for (int i = 0; i < NUM_DIGITAL_OUT; i++) json += String(digitalOutputs[i]) + (i < NUM_DIGITAL_OUT - 1 ? "," : "");
  json += "\"]}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);

  // Verificar si es la primera carga o hay datos corruptos
  uint8_t initializedFlag = EEPROM.read(512 - 1); // Último byte para la bandera
  bool configValid = loadConfig();
  if (initializedFlag != 0xAA || !configValid) {
    Serial.println("Primera carga o datos corruptos detectados. Limpiando EEPROM...");
    clearEEPROM();
    EEPROM.write(512 - 1, 0xAA); // Marcar como inicializado
    EEPROM.commit();
  } else {
    Serial.println("EEPROM válida, manteniendo configuración.");
  }

  Serial.println("Configurando pines digitales...");
  for (int i = 0; i < NUM_DIGITAL_OUT; i++) {
    pinMode(DIGITAL_OUT[i], OUTPUT);
    digitalWrite(DIGITAL_OUT[i], LOW);
    digitalOutputs[i] = false;
  }
  for (int i = 0; i < NUM_DIGITAL_IN; i++) {
    pinMode(DIGITAL_IN[i], INPUT_PULLUP);
    digitalInputs[i] = digitalRead(DIGITAL_IN[i]);
  }

  setupWiFi();

  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/sdStatus", handleSDStatus);
  server.on("/wifiStatus", handleWiFiStatus);
  server.on("/setOutput", handleSetOutput);
  server.on("/tareSensor", handleTareSensor);
  server.on("/toggleMode", handleToggleMode);
  server.on("/saveConfig", HTTP_POST, handleSaveConfig);
  server.on("/downloadData", handleDownloadData);
  server.on("/viewData", handleViewData);
  server.begin();
  Serial.println("Servidor web iniciado.");
}

void loop() {
  static unsigned long lastReconnectAttempt = 0;
  const unsigned long reconnectInterval = 300000; // 5 minutos

  server.handleClient();

  // Verificar si STA está desconectado
  if (!config.forceAPMode && WiFi.status() != WL_CONNECTED && wifiConnected) {
    Serial.println("Conexión STA perdida. Intentando reconectar...");
    wifiConnected = false;
    setupWiFi();
    return;
  }

  // Reintentar conexión STA si está en modo AP
  if (config.forceAPMode && millis() - lastReconnectAttempt > reconnectInterval) {
    Serial.println("Intentando reconectar en modo STA...");
    lastReconnectAttempt = millis();
    config.forceAPMode = false;
    WiFi.disconnect(true);
    delay(100);
    setupWiFi();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Fallo al reconectar en modo STA. Volviendo a modo AP...");
      config.forceAPMode = true;
      setupWiFi();
    }
  }
}
