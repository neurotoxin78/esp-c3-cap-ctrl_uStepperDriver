#include "A4988.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP32Ping.h>
#include <RGBLed.h>
#include <WebServer.h>
#include <WiFi.h>
#define PIN_WHITE GPIO_NUM_19
#define PIN_YELLOW GPIO_NUM_18
#define PIN_BLUE GPIO_NUM_3
#define PIN_RED GPIO_NUM_5
#define PIN_GREEN GPIO_NUM_4
#define PIN_DIR GPIO_NUM_6
#define PIN_STEP GPIO_NUM_7
#define PIN_EN GPIO_NUM_8
#define ENDSTOP GPIO_NUM_10
#define MOTOR_STEPS 200
#define RPM 50
#define MICROSTEPS 16
#define MOTOR_ACCEL 6000
#define MOTOR_DECEL 3500
#define STEPS 8000
#define S_DELAY_MS 100
#define HTTP_REST_PORT 8080
#define AP_SSID "Neurotoxin2"
#define AP_PASS "Mxbb2Col"

hw_timer_t *endstop_timer = NULL;

const char *hostname = "magloop-ctrl";
bool flag = false;
bool step_delay = true;
int step_count = 0;
int max_step = 7000;

WebServer server(HTTP_REST_PORT);

A4988 stepper(MOTOR_STEPS, PIN_DIR, PIN_STEP, PIN_EN);

RGBLed led(PIN_BLUE, PIN_GREEN, PIN_RED, RGBLed::COMMON_CATHODE);

void IRAM_ATTR onEndstopTimer()
{
  if (!digitalRead(ENDSTOP))
  {
    stepper.stop();
  }
}

void statusResponce(String status)
{
  DynamicJsonDocument doc(512);
  doc["status"] = status;
  doc["step_count"] = step_count;
  doc["endstop"] = digitalRead(ENDSTOP);
  doc["stepper_microstep"] = stepper.getMicrostep();
  String buf;
  serializeJson(doc, buf);
  server.send(200, F("application/json"), buf);
}

void moveTo(int dir, int step, int accel, int decel)
{
  stepper.setSpeedProfile(stepper.LINEAR_SPEED, accel, decel);
  stepper.enable();
  if (dir == 0)
  {
    stepper.move(step);
    step_count += step;
  }
  else
  {
    stepper.move(-step);
    step_count -= step;
  }
  stepper.disable();
}

void setMove()
{
  int dir, step, accel, decel;

  String postBody = server.arg("plain");
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, postBody);
  if (error)
  {
    String msg = error.c_str();
    server.send(400, F("text/html"),
                "Error in parsin json body! <br>" + msg);
  }
  else
  {
    JsonObject postObj = doc.as<JsonObject>();
    if (server.method() == HTTP_POST)
    {
      if (postObj.containsKey("direction") && postObj.containsKey("step") && postObj.containsKey("acceleration") && postObj.containsKey("deceleration"))
      {
        // Here store data or doing operation
        dir = int(postObj[F("direction")]);
        step = int(postObj[F("step")]);
        accel = int(postObj[F("acceleration")]);
        decel = int(postObj[F("deceleration")]);

        if (digitalRead(ENDSTOP) == true)
        {
          if (step_count <= max_step)
          {
            moveTo(dir, step, accel, decel);
            statusResponce("Complete");
          }
          else
          {
            statusResponce("Maximum position reached");
          }
        }
        else
        {
          statusResponce("Endstop Triggered!");
        }
      }
    }
    else
    {
      DynamicJsonDocument doc(512);
      doc["status"] = "KO";
      doc["message"] = F("No data found, or incorrect!");
      String buf;
      serializeJson(doc, buf);

      server.send(400, F("application/json"), buf);
    }
  }
}

void getPark()
{
  while (digitalRead(ENDSTOP))
  {
    moveTo(0, 5, 1000, 1000);
    delayMicroseconds(10);
  }
  statusResponce("Parked");
}

void getInfo()
{
  String info = ESP.getChipModel();
  statusResponce(info);
}

void restServerRouting()
{
  server.on("/", HTTP_GET, []()
            { server.send(200, F("text/html"),
                          F("Variable Capacitor Controller Web Server")); });
  server.on(F("/park"), HTTP_GET, getPark);
  server.on(F("/info"), HTTP_GET, getInfo);
  server.on(F("/move"), HTTP_POST, setMove);
}

void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void Task_Ping(void *pvParameters)
{
  const IPAddress remote_ip(10, 175, 1, 1);
  (void)pvParameters;
  while (1)
  {
    if (Ping.ping(remote_ip))
    {
      Serial.println("Success!!");
    }
    else
    {
      Serial.println("Error :(");
      ESP.restart();
    }
    vTaskDelay(30000 / portTICK_PERIOD_MS);
  }
}

void Task_HEARTBEAT(void *pvParameters)
{
  (void)pvParameters;
  while (1)
  {
    led.flash(RGBLed::CYAN, 20);
    vTaskDelay(2500 / portTICK_PERIOD_MS);
  }
}

void initHardware()
{
  pinMode(ENDSTOP, INPUT);
  pinMode(PIN_WHITE, OUTPUT);
  pinMode(PIN_YELLOW, OUTPUT);
  led.brightness(RGBLed::YELLOW, 25);
  led.flash(RGBLed::YELLOW, 200);
  digitalWrite(PIN_YELLOW, LOW);
  digitalWrite(PIN_WHITE, LOW);
  btStop();
}

void initStepperDriver()
{
  stepper.begin(RPM, MICROSTEPS);
  stepper.setEnableActiveState(LOW);
  stepper.setSpeedProfile(stepper.LINEAR_SPEED, MOTOR_ACCEL, MOTOR_DECEL);
  stepper.setMicrostep(16);
}

void initTimers()
{
  endstop_timer = timerBegin(0, 80, true);
  timerAttachInterrupt(endstop_timer, &onEndstopTimer, true);
  timerAlarmWrite(endstop_timer, S_DELAY_MS, true);
  timerAlarmEnable(endstop_timer);
}

void setup()
{
  Serial.begin(115200);
  initHardware();
  initStepperDriver();
  WiFi.mode(WIFI_STA);
  WiFi.begin(AP_SSID, AP_PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    led.flash(RGBLed::YELLOW, 100);
    delay(500);
  }

  Serial.println(WiFi.localIP());
  restServerRouting();
  // Set not found response
  server.onNotFound(handleNotFound);
  // Start server
  server.begin();
  Serial.println("Server Started!");

  initTimers();

  xTaskCreatePinnedToCore(Task_HEARTBEAT, "Task_HEARTBEAT", 4096, NULL, 3, NULL, ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(Task_Ping, "Task_Ping", 4096, NULL, 3, NULL, ARDUINO_RUNNING_CORE);
}

void loop()
{
  server.handleClient();
}
