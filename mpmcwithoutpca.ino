#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <Servo.h>

// WiFi credentials
const char* ssid = "Redmi";
const char* password = "hello123";

// Servo configuration
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
#define SERVOMIN  150  // Minimum pulse length count
#define SERVOMAX  600  // Maximum pulse length count
#define SERVO_FREQ 50  // Analog servos run at ~50 Hz updates

// Motor configuration
#define IN1 D4  // Motor input 1 (GPIO14)
#define IN2 D7  // Motor input 2 (GPIO12)

// Servo pin definitions
#define SERVO1_PIN 15  // D8 - Pan
#define SERVO2_PIN 12  // D6 - Tilt
#define SERVO3_PIN 14  // D5 - Grappler
#define SERVO4_PIN 0   // D3 - Arm

// Servo direct pin control
Servo servo1;  
Servo servo2;  
Servo servo3;  
Servo servo4;  

// AsyncWebServer on port 80
AsyncWebServer server(80);

// Struct to store servo positions with default values
struct ServoPositions {
    int pan = 90;
    int tilt = 90;
    int grappler = 90;
    int arm = 90;
} servoPositions;

// HTML content (stored in flash memory)
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Robotic Arm Controller</title>
    <style>
        body { 
            font-family: Arial, sans-serif; 
            max-width: 600px; 
            margin: 0 auto; 
            padding: 20px; 
            background: #f4f4f4; 
        }
        .control-box {
            background: white;
            border-radius: 8px;
            box-shadow: 0 4px 6px rgba(0,0,0,0.1);
            padding: 15px;
            margin-bottom: 15px;
        }
        .slider-container {
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .slider { flex-grow: 1; }
        .value { min-width: 50px; text-align: right; }
        button {
            width: 30%;
            padding: 10px;
            margin: 5px;
            background-color: #007BFF;
            color: white;
            border: none;
            border-radius: 4px;
            cursor: pointer;
        }
        button:hover { background-color: #0056b3; }
        button:disabled { 
            background-color: #cccccc; 
            cursor: not-allowed; 
        }
    </style>
</head>
<body>
    <div class="control-box">
        <h2>Vertical Control</h2>
        <div>
            <button onclick="controlMotor('forward')">Pull Up</button>
            <button onclick="controlMotor('reverse')">Pull Down</button>
            <button onclick="controlMotor('stop')">Stop</button>
        </div>
    </div>
    
    <div id="servo-controls"></div>
    
    <script>
        // Map the new names to the original servo identifiers
        const servoConfig = [
            { name: 'Rotate', id: 'pan' },
            { name: 'Grappler', id: 'tilt' },
            { name: 'Arm 2', id: 'grappler' },
            { name: 'Arm 1', id: 'arm' }
        ];
        
        const servoControls = document.getElementById('servo-controls');
        
        servoConfig.forEach(servo => {
            const controlBox = document.createElement('div');
            controlBox.classList.add('control-box');
            controlBox.innerHTML = `
                <h2>${servo.name}</h2>
                <div class="slider-container">
                    <input type="range" min="0" max="180" value="90" class="slider" id="${servo.id}">
                    <span class="value" id="${servo.id}-value">90°</span>
                </div>`;
            servoControls.appendChild(controlBox);
            
            const slider = controlBox.querySelector('.slider');
            const valueDisplay = controlBox.querySelector('.value');
            
            slider.addEventListener('input', () => {
                valueDisplay.textContent = slider.value + '°';
                updateServo(servo.id, slider.value);
            });
        });

        let updateTimeout = null;
        function updateServo(servo, value) {
            clearTimeout(updateTimeout);
            updateTimeout = setTimeout(() => {
                fetch(/servo/${servo}?value=${value}, {
                    method: 'POST',
                }).catch(error => console.error('Error:', error));
            }, 50);
        }

        function controlMotor(direction) {
            fetch(/motor/${direction}, { method: 'POST' })
                .catch(error => console.error('Error:', error));
        }
    </script>
</body>
</html>)rawliteral";

// Function to configure servo with min/max pulse width
void configureServo(Servo &servo, int pin, int minPulse = 500, int maxPulse = 2500) {
    servo.attach(pin, minPulse, maxPulse);
}

// Function to safely update servo position
void updateServo(Servo &servo, int &currentPos, int newPos) {
    // Add soft limits and smoothing
    newPos = constrain(newPos, 0, 180);
    if (abs(newPos - currentPos) > 1) {
        // Smooth movement
        currentPos = (currentPos + newPos) / 2;
        servo.write(currentPos);
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialize I2C and PCA9685
    Wire.begin(D2, D1);  // SDA = D2 (GPIO4), SCL = D1 (GPIO5)
    pwm.begin();
    pwm.setPWMFreq(SERVO_FREQ);
    
    // Initialize motor pins
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);

    // Configure servos with extended pulse range
    configureServo(servo1, SERVO1_PIN);  // D8 - Pan
    configureServo(servo2, SERVO2_PIN);  // D6 - Tilt
    configureServo(servo3, SERVO3_PIN);  // D5 - Grappler
    configureServo(servo4, SERVO4_PIN);  // D3 - Arm

    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Initialize mDNS
    if (MDNS.begin("robotic-arm")) {
        Serial.println("mDNS responder started");
    }

    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", INDEX_HTML);
    });

    // Handle servo control endpoints
    server.on("/servo/pan", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("value")) {
            int value = request->getParam("value")->value().toInt();
            updateServo(servo1, servoPositions.pan, value);
            request->send(200);
        } else {
            request->send(400, "text/plain", "Missing value parameter");
        }
    });

    server.on("/servo/tilt", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("value")) {
            int value = request->getParam("value")->value().toInt();
            updateServo(servo2, servoPositions.tilt, value);
            request->send(200);
        } else {
            request->send(400, "text/plain", "Missing value parameter");
        }
    });

    server.on("/servo/grappler", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("value")) {
            int value = request->getParam("value")->value().toInt();
            updateServo(servo3, servoPositions.grappler, value);
            request->send(200);
        } else {
            request->send(400, "text/plain", "Missing value parameter");
        }
    });

    server.on("/servo/arm", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("value")) {
            int value = request->getParam("value")->value().toInt();
            updateServo(servo4, servoPositions.arm, value);
            request->send(200);
        } else {
            request->send(400, "text/plain", "Missing value parameter");
        }
    });

    // Motor control endpoints
    server.on("/motor/forward", HTTP_POST, [](AsyncWebServerRequest *request) {
        digitalWrite(IN1, HIGH);
        digitalWrite(IN2, LOW);
        request->send(200);
    });

    server.on("/motor/reverse", HTTP_POST, [](AsyncWebServerRequest *request) {
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, HIGH);
        request->send(200);
    });

    server.on("/motor/stop", HTTP_POST, [](AsyncWebServerRequest *request) {
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, LOW);
        request->send(200);
    });

    // Start server
    server.begin();
}

void loop() {
    // Handle mDNS
    MDNS.update();
    
    // Yield to prevent watchdog timer reset
    yield();
}