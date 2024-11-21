#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// WiFi credentials
const char* ssid = "gugugagabotcontrols";
const char* password = "12345678";

// Servo configuration
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
#define SERVOMIN  150  // This is the 'minimum' pulse length count
#define SERVOMAX  600  // This is the 'maximum' pulse length count
#define SERVO_FREQ 50  // Analog servos run at ~50 Hz updates

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Store servo positions
struct {
    int pan = 90;
    int tilt = 90;
    int grappler = 90;
    int arm1 = 90;
    int arm2 = 90;
    int arm3 = 90;
} servoPositions;

// HTML content (stored in flash memory)
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 20px;
            background: #f0f0f0;
        }
        .control-box {
            background: white;
            padding: 15px;
            margin-bottom: 15px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        .slider-container {
            display: flex;
            align-items: center;
            gap: 10px;
            margin-top: 10px;
        }
        .slider {
            flex-grow: 1;
            height: 25px;
        }
        .value {
            min-width: 50px;
            text-align: right;
        }
        h2 {
            color: #333;
            margin: 0;
            font-size: 1.2em;
        }
    </style>
</head>
<body>
    <div class="control-box">
        <h2>Pan</h2>
        <div class="slider-container">
            <input type="range" min="0" max="180" value="90" class="slider" id="pan" 
                oninput="updateServo('pan', this.value)">
            <span class="value" id="pan-value">90°</span>
        </div>
    </div>
    
    <div class="control-box">
        <h2>Tilt</h2>
        <div class="slider-container">
            <input type="range" min="0" max="180" value="90" class="slider" id="tilt" 
                oninput="updateServo('tilt', this.value)">
            <span class="value" id="tilt-value">90°</span>
        </div>
    </div>
    
    <div class="control-box">
        <h2>Grappler</h2>
        <div class="slider-container">
            <input type="range" min="0" max="180" value="90" class="slider" id="grappler" 
                oninput="updateServo('grappler', this.value)">
            <span class="value" id="grappler-value">90°</span>
        </div>
    </div>
    
    <div class="control-box">
        <h2>Arm 1</h2>
        <div class="slider-container">
            <input type="range" min="0" max="180" value="90" class="slider" id="arm1" 
                oninput="updateServo('arm1', this.value)">
            <span class="value" id="arm1-value">90°</span>
        </div>
    </div>
    
    <div class="control-box">
        <h2>Arm 2</h2>
        <div class="slider-container">
            <input type="range" min="0" max="180" value="90" class="slider" id="arm2" 
                oninput="updateServo('arm2', this.value)">
            <span class="value" id="arm2-value">90°</span>
        </div>
    </div>
    
    <div class="control-box">
        <h2>Arm 3</h2>
        <div class="slider-container">
            <input type="range" min="0" max="180" value="90" class="slider" id="arm3" 
                oninput="updateServo('arm3', this.value)">
            <span class="value" id="arm3-value">90°</span>
        </div>
    </div>

    <script>
        let updateTimeout = null;
        function updateServo(servo, value) {
            document.getElementById(servo + '-value').textContent = value + '°';
            
            // Debounce the HTTP request
            clearTimeout(updateTimeout);
            updateTimeout = setTimeout(() => {
                fetch(/servo/${servo}?value=${value}, {
                    method: 'POST',
                }).catch(error => console.error('Error:', error));
            }, 50);
        }
    </script>
</body>
</html>
)rawliteral";

// Function to map angle to servo pulse length
int angleToPluse(int angle) {
    return map(angle, 0, 180, SERVOMIN, SERVOMAX);
}

void setup() {
    Serial.begin(115200);
    
    // Initialize I2C and PCA9685
    Wire.begin(D2, D1);  // SDA = D2 (GPIO4), SCL = D1 (GPIO5)
    pwm.begin();
    pwm.setPWMFreq(SERVO_FREQ);
    
    // Set initial servo positions
    for(int i = 0; i < 6; i++) {
        pwm.setPWM(i, 0, angleToPluse(90));
    }

    // Connect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html", INDEX_HTML);
    });

    // Handle servo control endpoints
    server.on("/servo/pan", HTTP_POST, [](AsyncWebServerRequest *request) {
        String valueStr = request->getParam("value")->value();
        servoPositions.pan = valueStr.toInt();
        pwm.setPWM(0, 0, angleToPluse(servoPositions.pan));
        request->send(200);
    });

    server.on("/servo/tilt", HTTP_POST, [](AsyncWebServerRequest *request) {
        String valueStr = request->getParam("value")->value();
        servoPositions.tilt = valueStr.toInt();
        pwm.setPWM(1, 0, angleToPluse(servoPositions.tilt));
        request->send(200);
    });

    server.on("/servo/grappler", HTTP_POST, [](AsyncWebServerRequest *request) {
        String valueStr = request->getParam("value")->value();
        servoPositions.grappler = valueStr.toInt();
        pwm.setPWM(2, 0, angleToPluse(servoPositions.grappler));
        request->send(200);
    });

    server.on("/servo/arm1", HTTP_POST, [](AsyncWebServerRequest *request) {
        String valueStr = request->getParam("value")->value();
        servoPositions.arm1 = valueStr.toInt();
        pwm.setPWM(3, 0, angleToPluse(servoPositions.arm1));
        request->send(200);
    });

    server.on("/servo/arm2", HTTP_POST, [](AsyncWebServerRequest *request) {
        String valueStr = request->getParam("value")->value();
        servoPositions.arm2 = valueStr.toInt();
        pwm.setPWM(4, 0, angleToPluse(servoPositions.arm2));
        request->send(200);
    });

    server.on("/servo/arm3", HTTP_POST, [](AsyncWebServerRequest *request) {
        String valueStr = request->getParam("value")->value();
        servoPositions.arm3 = valueStr.toInt();
        pwm.setPWM(5, 0, angleToPluse(servoPositions.arm3));
        request->send(200);
    });

    // Start server
    server.begin();
}

void loop() {
    // The async web server handles requests in the background
    yield();  // Allow the ESP8266 to handle background tasks
}