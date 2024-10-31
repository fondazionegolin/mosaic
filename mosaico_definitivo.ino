#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

// Configurazione WiFi
const char* ssid = "ServoControlHotspot";
const char* password = "12345678";

// Configurazione Server Web
WebServer server(80);

// Configurazione PCA9685
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// Costanti per i servo
#define SERVOMIN  150 // Valore minimo pulse length (45 gradi)
#define SERVOMAX  600 // Valore massimo pulse length (135 gradi)
#define NUM_SERVOS 16

// Array per memorizzare le posizioni correnti e target
int currentPositions[NUM_SERVOS];
int targetPositions[NUM_SERVOS];

// Variabili per il controllo del movimento dei servi
unsigned long servoMoveTimestamp[NUM_SERVOS];
int globalSpeed = 5; // Velocità globale (1-10)

// Variabili per l'esecuzione delle posizioni salvate
bool isRunning = false;
bool isLooping = false;
#define MAX_SAVED_POSITIONS 10

struct SavedPosition {
  int angles[NUM_SERVOS];
  int speed;
};

SavedPosition savedPositions[MAX_SAVED_POSITIONS];
int savedCount = 0;
unsigned long lastPositionTime = 0;
int currentPositionIndex = 0;
unsigned long positionDelay = 500; // Delay tra le posizioni salvate (in millisecondi)

// **Aggiunta di una variabile per gestire lo stato di esecuzione nel JavaScript**
bool isExecuting = false;

const char WEBPAGE[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>
<head>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Fredoka:wght@300..700&display=swap" rel="stylesheet">
    <title>Controllo Griglia Servomotori</title>
    <style>
        body { font-family: Fredoka, sans-serif; text-align: center; }

        .grid-container {
            background-color: #2Ea8e7;
            border-radius: 25px;
            padding: 20px;
            display: inline-block;
            box-shadow: 0 0 20px rgba(0,0,0,0.1);
        }

        .grid {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 30px;
            justify-content: center;
        }

        .servo-cell {
            border-radius: 20px;
            width: 90px;
            height: 90px;
            padding: 5px;
            text-align: center;
            box-shadow: 0 0 25px rgba(0,0,0,0.1);
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            background: #fefefe;
        }

        .servo-cell input[type="range"] {
            width: 60px;
        }

        .servo-name {
            color: lightgray;
        }

        .angle-display {
            font-weight: bold;
            font-size: larger;
        }

        .controls {
            margin-top: 20px;
        }

        #globalAngle {
            font-size: larger;
            width: 100px;
            border-radius: 25px;
            border: none;
            outline: none;
            padding: 10px;
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }

        #speedSlider {
            width: 200px;
            margin-right: 10px;
        }

        #speedValue {
            font-weight: bold;
            margin-right: 20px;
        }

        button {
            padding: 10px;
            margin: 5px;
            cursor: pointer;
            border: none;
            outline: none;
            border-radius: 5px;
            font-weight: bold;
            color: white;
        }

        .btn-green {
            background-color: #77dd77;
        }

        .btn-red {
            background-color: #ff6961;
        }

        .btn-navy {
            background-color: #000080;
        }

        .btn-blue {
            background-color: #aec6cf;
        }

        table {
            margin: 20px auto;
            display: inline-block;
            box-shadow: 0 0 15px rgba(0,0,0,0.1);
            border-radius: 15px;
            overflow: hidden;
        }

        th, td {
            padding: 5px;
            text-align: center;
            border: 1px solid #ccc;
        }

        th {
            background-color: red;
            color: white;
            font-weight: bold;
        }

        tbody tr:nth-child(odd) {
            background-color: #f9f9f9;
        }

        tbody tr:nth-child(even) {
            background-color: white;
        }

        .bottom-controls {
            margin-top: 20px;
            display: flex;
            justify-content: flex-end;
            gap: 10px;
        }
    </style>
</head>
<body>
    <h1>Controllo Griglia Servomotori</h1>
    <div class="grid-container">
        <div class="grid" id="servoGrid"></div>
    </div>
    <div class="controls">
        <input type="number" id="globalAngle" min="45" max="135" value="90">
        <button onclick="setAllServos()" class="btn-green">Imposta Tutti</button>
        <input type="range" id="speedSlider" min="1" max="10" value="5">
        <span id="speedValue">5</span>
        <button onclick="savePosition()" class="btn-navy">Salva Posizione</button>
    </div>
    <div id="tableContainer">
        <table id="savedPositionsTable">
            <thead>
                <tr>
                    <th>Posizione #</th>
                    <!-- Genera le intestazioni per i servi -->
                    <script>
                        for (let i = 1; i <= 16; i++) {
                            document.write('<th>' + i + '</th>');
                        }
                    </script>
                    <th>Velocità</th>
                </tr>
            </thead>
            <tbody></tbody>
        </table>
    </div>
    <button onclick="executeSavedPositions()" class="btn-green">Esegui</button>
    <button onclick="loopSavedPositions()" class="btn-navy">Loop</button>
    <button onclick="stopProgram()" class="btn-red">Stop</button>
    <div class="bottom-controls">
        <button onclick="saveTableToCsv()" class="btn-blue">Salva in CSV</button>
        <input type="file" id="fileInput" accept=".csv" onchange="importTableFromCsv()" style="display: none;">
        <button onclick="document.getElementById('fileInput').click()" class="btn-blue">Importa</button>
    </div>

    <script>
        let isExecuting = false; // Variabile per tenere traccia se il programma è in esecuzione

        // Inizializzazione griglia
        function initGrid() {
            const grid = document.getElementById('servoGrid');
            for (let i = 0; i < 16; i++) {
                const cell = document.createElement('div');
                cell.className = 'servo-cell';
                cell.innerHTML = `
                    <div class="servo-name">Servo ${i + 1}</div>
                    <input type="range" min="45" max="135" value="90" oninput="updateServo(${i}, this.value)">
                    <div class="angle-display">90</div>
                `;
                grid.appendChild(cell);
            }
        }

        document.getElementById('speedSlider').addEventListener('input', function() {
            document.getElementById('speedValue').textContent = this.value;
            // Invia la velocità al server
            setGlobalSpeed(this.value);
        });

        async function setGlobalSpeed(speed) {
            await fetch('/api/setspeed', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ speed: parseInt(speed) })
            });
        }

        async function updateServo(index, angle) {
            if (angle < 45 || angle > 135) return;
            await fetch('/api/servo', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ servo: index, angle: parseInt(angle) })
            });
            document.querySelectorAll('.servo-cell')[index].querySelector('.angle-display').textContent = angle;
        }

        async function setAllServos() {
            const angle = parseInt(document.getElementById('globalAngle').value);
            if (angle < 45 || angle > 135) return;
            await fetch('/api/all', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ angle: angle })
            });
        }

        async function stopProgram() { 
            await fetch('/api/stop', { method: 'POST' }); 
            isExecuting = false;
            updateTableEditability();
        }

        async function savePosition() {
            const speed = parseInt(document.getElementById('speedSlider').value);
            const response = await fetch('/api/save', {
                method: 'POST',
                headers: {'Content-Type': 'application/json'},
                body: JSON.stringify({ speed: speed })
            });
            const data = await response.json();
            if (data.status === 'position_saved') {
                updateSavedPositionsTable();
            }
        }

        async function updateSavedPositionsTable() {
            const response = await fetch('/api/getsaved', { method: 'GET' });
            const data = await response.json();
            const tableBody = document.getElementById('savedPositionsTable').querySelector('tbody');
            tableBody.innerHTML = '';
            data.positions.forEach((positionData, index) => {
                const position = positionData.angles;
                const speed = positionData.speed;
                const row = document.createElement('tr');
                let rowContent = `<td>${index + 1}</td>`;
                position.forEach((angle, i) => {
                    rowContent += `<td class="editable-cell" data-row="${index}" data-col="${i}">${angle}</td>`;
                });
                rowContent += `<td class="editable-cell" data-row="${index}" data-col="speed">${speed}</td>`;
                row.innerHTML = rowContent;
                tableBody.appendChild(row);
            });
            addCellEventListeners();
        }

        function addCellEventListeners() {
            const cells = document.querySelectorAll('.editable-cell');
            cells.forEach(cell => {
                cell.addEventListener('click', function() {
                    if (isExecuting) return; // Non permette modifiche durante l'esecuzione
                    const row = this.getAttribute('data-row');
                    const col = this.getAttribute('data-col');
                    const oldValue = this.textContent;
                    const input = document.createElement('input');
                    input.type = 'number';
                    input.value = oldValue;
                    input.style.width = '60px';
                    input.addEventListener('blur', function() {
                        saveCellValue(row, col, input.value);
                        cell.textContent = input.value;
                    });
                    input.addEventListener('keydown', function(e) {
                        if (e.key === 'Enter') {
                            saveCellValue(row, col, input.value);
                            cell.textContent = input.value;
                        }
                    });
                    this.textContent = '';
                    this.appendChild(input);
                    input.focus();
                });
            });
        }

        async function saveCellValue(row, col, value) {
            value = parseInt(value);
            if (col === 'speed') {
                // Aggiorna la velocità della posizione
                await fetch('/api/updateSpeed', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ index: parseInt(row), speed: value })
                });
            } else {
                // Aggiorna l'angolo del servo specifico
                if (value < 45 || value > 135) {
                    alert('Valore angolare fuori dai limiti (45-135)');
                    return;
                }
                await fetch('/api/updateAngle', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ index: parseInt(row), servo: parseInt(col), angle: value })
                });
            }
        }

        async function executeSavedPositions() {
            isExecuting = true;
            updateTableEditability();
            await fetch('/api/execute', { method: 'POST' });
        }

        async function loopSavedPositions() {
            isExecuting = true;
            updateTableEditability();
            await fetch('/api/loop', { method: 'POST' });
        }

        function updateTableEditability() {
            const cells = document.querySelectorAll('.editable-cell');
            cells.forEach(cell => {
                if (isExecuting) {
                    cell.style.pointerEvents = 'none';
                    cell.style.backgroundColor = '#e0e0e0';
                } else {
                    cell.style.pointerEvents = 'auto';
                    cell.style.backgroundColor = '';
                }
            });
        }

        function saveTableToCsv() {
            const tableBody = document.getElementById('savedPositionsTable').querySelector('tbody');
            let csvContent = 'Posizione #';
            for (let i = 1; i <= 16; i++) {
                csvContent += ',Servo ' + i;
            }
            csvContent += ',Velocità\n';
            tableBody.querySelectorAll('tr').forEach((row, index) => {
                let rowData = [];
                row.querySelectorAll('td').forEach(cell => {
                    rowData.push(cell.textContent);
                });
                csvContent += rowData.join(',') + '\n';
            });
            const blob = new Blob([csvContent], { type: 'text/csv' });
            const a = document.createElement('a');
            a.href = URL.createObjectURL(blob);
            a.download = 'positions.csv';
            a.click();
        }

        function importTableFromCsv() {
            const fileInput = document.getElementById('fileInput');
            const file = fileInput.files[0];
            if (!file) return;

            const reader = new FileReader();
            reader.onload = async function(event) {
                const csvContent = event.target.result;
                const rows = csvContent.split('\n').slice(1).filter(row => row.trim() !== '');
                const tableBody = document.getElementById('savedPositionsTable').querySelector('tbody');
                tableBody.innerHTML = '';
                await fetch('/api/clear', { method: 'POST' }); // Clear saved positions on server
                for (let index = 0; index < rows.length; index++) {
                    const row = rows[index];
                    const columns = row.split(',');
                    const positionNumber = columns[0];
                    const angles = columns.slice(1, 17).map(Number);
                    const speed = parseInt(columns[17]);
                    const tableRow = document.createElement('tr');
                    let rowContent = `<td>${positionNumber}</td>`;
                    angles.forEach((angle, i) => {
                        rowContent += `<td class="editable-cell" data-row="${index}" data-col="${i}">${angle}</td>`;
                    });
                    rowContent += `<td class="editable-cell" data-row="${index}" data-col="speed">${speed}</td>`;
                    tableRow.innerHTML = rowContent;
                    tableBody.appendChild(tableRow);
                    // Salva la posizione importata nel server
                    await fetch('/api/saveImported', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({ angles: angles, speed: speed })
                    });
                }
                addCellEventListeners();
            };
            reader.readAsText(file);
        }

        initGrid();
        updateSavedPositionsTable();
    </script>
</body>
</html>
)=====";

void handleRoot() { server.send(200, "text/html", WEBPAGE); }

int angleToPulse(int angle) {
  return map(angle, 0, 180, SERVOMIN, SERVOMAX);
}

void setServoAngle(int servo, int angle) {
  if (servo >= 0 && servo < NUM_SERVOS) {
    targetPositions[servo] = angle; // Imposta l'angolo target
    Serial.print("Servo ");
    Serial.print(servo);
    Serial.print(" target angle set to: ");
    Serial.println(angle);
  }
}

void handleSetSpeed() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, server.arg("plain"));
    int speed = doc["speed"];
    if (speed >= 1 && speed <= 10) {
      globalSpeed = speed;
      server.send(200, "application/json", "{\"status\":\"ok\"}");
      Serial.print("Global speed set to: ");
      Serial.println(globalSpeed);
    } else {
      server.send(400, "application/json", "{\"status\":\"invalid_speed\"}");
    }
  }
}

void handleUpdateSpeed() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, server.arg("plain"));
    int index = doc["index"];
    int speed = doc["speed"];
    if (index >= 0 && index < savedCount) {
      savedPositions[index].speed = speed;
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      server.send(400, "application/json", "{\"status\":\"invalid_index\"}");
    }
  }
}

void handleUpdateAngle() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, server.arg("plain"));
    int index = doc["index"];
    int servo = doc["servo"];
    int angle = doc["angle"];
    if (index >= 0 && index < savedCount && servo >= 0 && servo < NUM_SERVOS) {
      savedPositions[index].angles[servo] = angle;
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      server.send(400, "application/json", "{\"status\":\"invalid_index_or_servo\"}");
    }
  }
}

void handleServo() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, server.arg("plain"));
    int servo = doc["servo"];
    int angle = doc["angle"];
    if (angle >= 45 && angle <= 135) {
      setServoAngle(servo, angle);
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      server.send(400, "application/json", "{\"status\":\"invalid_angle\"}");
    }
  }
}

void handleAll() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<200> doc;
    deserializeJson(doc, server.arg("plain"));
    int angle = doc["angle"];
    if (angle >= 45 && angle <= 135) {
      for (int i = 0; i < NUM_SERVOS; i++) {
        setServoAngle(i, angle);
      }
      server.send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      server.send(400, "application/json", "{\"status\":\"invalid_angle\"}");
    }
  }
}

void handleGetSaved() {
  Serial.println("Received command: /api/getsaved");
  StaticJsonDocument<8192> doc; // Aumentato per gestire più dati
  JsonArray positions = doc.createNestedArray("positions");
  for (int i = 0; i < savedCount; i++) {
    JsonObject positionData = positions.createNestedObject();
    JsonArray angles = positionData.createNestedArray("angles");
    for (int j = 0; j < NUM_SERVOS; j++) {
      angles.add(savedPositions[i].angles[j]);
    }
    positionData["speed"] = savedPositions[i].speed;
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleSave() {
  Serial.println("Received command: /api/save");
  if (savedCount < MAX_SAVED_POSITIONS) {
    int speed = globalSpeed; // Utilizza la velocità globale al momento del salvataggio
    if (server.hasArg("plain")) {
      StaticJsonDocument<200> doc;
      deserializeJson(doc, server.arg("plain"));
      speed = doc["speed"];
    }

    for (int i = 0; i < NUM_SERVOS; i++) {
      savedPositions[savedCount].angles[i] = currentPositions[i];
    }
    savedPositions[savedCount].speed = speed;
    savedCount++;
    server.send(200, "application/json", "{\"status\":\"position_saved\"}");
  } else {
    server.send(400, "application/json", "{\"status\":\"memory_full\"}");
  }
}

void handleStop() {
  Serial.println("Received command: /api/stop");
  isRunning = false;
  isLooping = false;
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleExecute() {
  Serial.println("Received command: /api/execute");
  isRunning = true;
  isLooping = false;
  currentPositionIndex = 0;
  lastPositionTime = millis();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleLoop() {
  Serial.println("Received command: /api/loop");
  isRunning = true;
  isLooping = true;
  currentPositionIndex = 0;
  lastPositionTime = millis();
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleClear() {
  Serial.println("Received command: /api/clear");
  savedCount = 0;
  server.send(200, "application/json", "{\"status\":\"positions_cleared\"}");
}

void handleSaveImported() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<2048> doc;
    deserializeJson(doc, server.arg("plain"));
    JsonArray angles = doc["angles"];
    int speed = doc["speed"];
    if (savedCount < MAX_SAVED_POSITIONS) {
      for (int i = 0; i < NUM_SERVOS; i++) {
        savedPositions[savedCount].angles[i] = angles[i];
      }
      savedPositions[savedCount].speed = speed;
      savedCount++;
      server.send(200, "application/json", "{\"status\":\"position_saved\"}");
    } else {
      server.send(400, "application/json", "{\"status\":\"memory_full\"}");
    }
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.begin(115200);
  pwm.begin();
  pwm.setPWMFreq(60);

  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  server.on("/", handleRoot);
  server.on("/api/servo", HTTP_POST, handleServo);
  server.on("/api/all", HTTP_POST, handleAll);
  server.on("/api/setspeed", HTTP_POST, handleSetSpeed);
  server.on("/api/updateSpeed", HTTP_POST, handleUpdateSpeed);
  server.on("/api/updateAngle", HTTP_POST, handleUpdateAngle);
  server.on("/api/getsaved", HTTP_GET, handleGetSaved);
  server.on("/api/save", HTTP_POST, handleSave);
  server.on("/api/stop", HTTP_POST, handleStop);
  server.on("/api/execute", HTTP_POST, handleExecute);
  server.on("/api/loop", HTTP_POST, handleLoop);
  server.on("/api/clear", HTTP_POST, handleClear);
  server.on("/api/saveImported", HTTP_POST, handleSaveImported);
  server.begin();

  for (int i = 0; i < NUM_SERVOS; i++) {
    currentPositions[i] = 90;
    targetPositions[i] = 90;
    pwm.setPWM(i, 0, angleToPulse(90));
    servoMoveTimestamp[i] = millis();
  }
}

void loop() {
  server.handleClient();

  unsigned long currentTime = millis();

  // Movimento non bloccante dei servomotori
  for (int i = 0; i < NUM_SERVOS; i++) {
    if (currentPositions[i] != targetPositions[i]) {
      // Mappa globalSpeed (1-10) a delay per passo (20ms a 5ms)
      int delayPerStep = map(globalSpeed, 1, 10, 20, 5);
      if (currentTime - servoMoveTimestamp[i] >= delayPerStep) {
        // Muovi il servo di un passo verso la posizione target
        if (currentPositions[i] < targetPositions[i]) {
          currentPositions[i]++;
        } else if (currentPositions[i] > targetPositions[i]) {
          currentPositions[i]--;
        }
        pwm.setPWM(i, 0, angleToPulse(currentPositions[i]));
        servoMoveTimestamp[i] = currentTime;
      }
    }
  }

  // Gestione dell'esecuzione delle posizioni salvate
  if (isRunning) {
    if (currentTime - lastPositionTime >= positionDelay) {
      if (currentPositionIndex < savedCount) {
        // Imposta le posizioni target ai valori salvati
        for (int i = 0; i < NUM_SERVOS; i++) {
          targetPositions[i] = savedPositions[currentPositionIndex].angles[i];
        }
        // Regola la velocità per questa posizione
        globalSpeed = savedPositions[currentPositionIndex].speed;
        currentPositionIndex++;
        lastPositionTime = currentTime;
      } else {
        if (isLooping) {
          currentPositionIndex = 0;
        } else {
          isRunning = false;
        }
      }
    }
  }
}
