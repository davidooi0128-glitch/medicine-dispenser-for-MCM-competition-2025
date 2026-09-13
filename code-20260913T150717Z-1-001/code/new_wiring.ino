#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <SoftwareSerial.h>
#include <DHT.h>
#include <EEPROM.h> 

// --- 📈 水位非线性校准参数（复用给两个水箱） ---
const int V_EMPTY = 180;    
const int V_HALF  = 630;  
const int V_FULL  = 688;  

// --- 🌡️ 温湿度软校准偏移量 (Software Calibration Offsets) ---
float tempOffset = 0.0;     // 温度偏移量 (°C)，例如偏低0.5度填 +0.5
float humiOffset = 0.0;     // 湿度偏移量 (%)

// --- 🆕 引脚定义 ---
const int PIN_MED_LEVEL = A0;       // 药水水位传感器 (模拟量 A0)
const int PIN_ESP_RX = 2;           // ESP8266 TX -> Arduino D2 (软串口 RX)
const int PIN_ESP_TX = 3;           // ESP8266 RX -> Arduino D3 (软串口 TX，带分压)
const int PIN_WATER_LEVEL = A3;     // 饮水水位传感器 (模拟量 A3)
const int PIN_MAIN_BUTTON = 4;      // 主出药按键 (数字量 D4)
const int PIN_BUZZER = 13;          // 蜂鸣器 (数字量 D13)
const int PIN_WATER_PUMP = 12;      // 饮水泵 / 继电器 IN2 (数字量 D12)
const int PIN_MED_PUMP = 5;         // 药水泵 / 继电器 IN1 (数字量 D5)
const int PIN_FILL_SWITCH = 6;      // 饮水加水行程开关 (数字量 D6)
const int PIN_MED_FILL_SWITCH = 7;  // 药水加水行程开关 (数字量 D7)
const int PIN_HALL = 8;             // 霍尔流量传感器 (数字量 D8)
const int PIN_WATER_BUTTON = 9;     // 取水按钮 (数字量 D9)
const int PIN_LASER = 10;           // 激光对射接收头 (数字量 D10)
const int PIN_SERVO = 11;           // 出药舵机 (PWM D11)
const int PIN_DHT = A1;             // DHT22 温湿度传感器 (数字量/模拟引脚 A1)

// --- Object Initialization ---
SoftwareSerial espSerial(PIN_ESP_RX, PIN_ESP_TX); 
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo myServo;
DHT dht(PIN_DHT, DHT22);

// --- State and Schedule Variables ---
int numDoses = 0;
int startTimes[3], endTimes[3], remindTimes[3], pulses[3], pills[3];
bool doseTaken[3] = {false, false, false};

unsigned long previousMillis = 0;
const unsigned long INTERVAL = 1000; 

volatile int hallPulseCount = 0; 
int lastVMin = 0; 

int getVirtualMinutes() {
  unsigned long virtualMillis = millis() * 288;
  return (virtualMillis / 60000) % 1440; 
}

int getLinearBlocks(int rawLevel) {
  int blocks = 0;
  if (rawLevel <= V_EMPTY) {
    blocks = 0;
  } else if (rawLevel <= V_HALF) {
    blocks = map(rawLevel, V_EMPTY, V_HALF, 0, 8);
  } else {
    blocks = map(rawLevel, V_HALF + 1, V_FULL, 8, 16);
  }
  
  if (blocks > 16) blocks = 16;
  if (blocks < 0) blocks = 0;
  return blocks;
}

void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);
  dht.begin();
  lcd.init();
  lcd.backlight();

  pinMode(PIN_MAIN_BUTTON, INPUT); 
  pinMode(PIN_WATER_BUTTON, INPUT); 
  pinMode(PIN_FILL_SWITCH, INPUT); 
  pinMode(PIN_MED_FILL_SWITCH, INPUT); 
  
  pinMode(PIN_LASER, INPUT_PULLUP); 
  pinMode(PIN_HALL, INPUT_PULLUP); 
  
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_WATER_PUMP, OUTPUT);
  pinMode(PIN_MED_PUMP, OUTPUT);
  
  digitalWrite(PIN_WATER_PUMP, LOW);
  digitalWrite(PIN_MED_PUMP, LOW);

  lcd.setCursor(0, 0);
  lcd.print("System Starting");
  delay(1000);

  loadFromEEPROM(); 
  lastVMin = getVirtualMinutes(); 
}

void loop() {
  receiveSchedule(); 

  int currentVMin = getVirtualMinutes();

  if (currentVMin < lastVMin) {
    for (int i = 0; i < 3; i++) doseTaken[i] = false; 
    lcd.clear();
    lcd.print("New Day Reset!");
    delay(1000);
    lcd.clear();
  }
  lastVMin = currentVMin; 

  // 执行综合水系统控制逻辑
  handleWaterSystem(currentVMin);
  
  // 仅在未开启任何加水和取水模式时，更新药丸闹钟判定
  if (digitalRead(PIN_FILL_SWITCH) == LOW && digitalRead(PIN_MED_FILL_SWITCH) == LOW && digitalRead(PIN_WATER_BUTTON) == LOW) {
    checkAlarms(currentVMin);
  }

  // 出药逻辑
  if (digitalRead(PIN_MAIN_BUTTON) == HIGH) {
    delay(50); 
    if (digitalRead(PIN_MAIN_BUTTON) == HIGH) {
      int activeDoseIndex = getActiveDoseIndex(currentVMin);
      
      if (activeDoseIndex != -1 && !doseTaken[activeDoseIndex]) {
        executeDispensing(activeDoseIndex);
      } else {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Not time yet!");
        delay(2000);
        lcd.clear();
      }
    }
  }
}

// --- 综合水系统管理（集成药水监控与闪烁） ---
void handleWaterSystem(int currentMins) {
  int waterLevel = analogRead(PIN_WATER_LEVEL);
  int medLevel = analogRead(PIN_MED_LEVEL); 
  
  int fillSwitchState = digitalRead(PIN_FILL_SWITCH);
  int medFillSwitchState = digitalRead(PIN_MED_FILL_SWITCH);
  int waterBtnState = digitalRead(PIN_WATER_BUTTON);

  static int lastMode = 0; 
  int currentMode = 0;

  // 模式判定
  if (fillSwitchState == HIGH) {
    currentMode = 1; // 饮水加水
  } else if (medFillSwitchState == HIGH) {
    currentMode = 3; // 药水加水
  } else if (waterBtnState == HIGH) {
    currentMode = 2; // 取水
  } else {
    currentMode = 0; // 待机
  }

  // 边缘触发检测清除残影
  if (currentMode != lastMode) {
    lcd.clear();          
    previousMillis = 0;   
    lastMode = currentMode;
  }

  // === 1. 饮水加水模式 ===
  if (currentMode == 1) {
    digitalWrite(PIN_WATER_PUMP, LOW); 
    lcd.setCursor(0, 0);
    lcd.print("Water Level     ");
    lcd.setCursor(0, 1);
    
    int numBlocks = getLinearBlocks(waterLevel); 
    for (int i = 0; i < 16; i++) {
      if (i < numBlocks) lcd.write(0xFF); 
      else lcd.print(" ");
    }
    
    if (waterLevel >= 750) tone(PIN_BUZZER, 1200); 
    else noTone(PIN_BUZZER);
  }
  
  // === 3. 药水加水模式 ===
  else if (currentMode == 3) {
    digitalWrite(PIN_MED_PUMP, LOW); 
    lcd.setCursor(0, 0);
    lcd.print("Med Level       ");
    lcd.setCursor(0, 1);
    
    int numBlocks = getLinearBlocks(medLevel); 
    for (int i = 0; i < 16; i++) {
      if (i < numBlocks) lcd.write(0xFF); 
      else lcd.print(" ");
    }
    
    if (medLevel >= 750) tone(PIN_BUZZER, 1200); 
    else noTone(PIN_BUZZER);
  }

  // === 2. 取水模式 ===
  else if (currentMode == 2) {
    if (waterLevel < 50) {
      digitalWrite(PIN_WATER_PUMP, LOW);
      lcd.setCursor(0, 0);
      lcd.print("Water Too Low!");
      for (int i = 0; i < 6; i++) {
        tone(PIN_BUZZER, 1500); delay(100); noTone(PIN_BUZZER); delay(230); 
      }
      lcd.clear();
    } else {
      digitalWrite(PIN_WATER_PUMP, HIGH);
      lcd.setCursor(0, 0);
      lcd.print("Dispensing...   ");
      lcd.setCursor(0, 1);
      int numBlocks = getLinearBlocks(waterLevel);
      for (int i = 0; i < 16; i++) {
        if (i < numBlocks) lcd.write(0xFF); else lcd.print(" ");
      }
    }
    if (isMedAlarmActive(currentMins) && waterLevel >= 50) tone(PIN_BUZZER, 1000);
    else if (waterLevel >= 50) noTone(PIN_BUZZER);
  }
  
  // === 0. 正常待机模式 ===
  else {
    digitalWrite(PIN_WATER_PUMP, LOW); 
    
    // 缺药水无阻塞闪烁逻辑
    if (medLevel < 50) {
      unsigned long currentMillis = millis();
      static unsigned long lastWarningMillis = 0;
      static bool isWarning = false;

      if (currentMillis - lastWarningMillis >= 1000) {
        lastWarningMillis = currentMillis;
        isWarning = !isWarning;
        lcd.clear();
        previousMillis = 0; 
      }

      if (isWarning) {
        lcd.setCursor(0, 0);
        lcd.print("Low Medicine!   ");
        lcd.setCursor(0, 1);
        lcd.print("Please Refill   ");
      } else {
        updateDisplay(currentMins); 
      }
    } 
    else {
      updateDisplay(currentMins);        
    }
    
    if (isMedAlarmActive(currentMins)) tone(PIN_BUZZER, 1000);
    else noTone(PIN_BUZZER);
  }
}

bool isMedAlarmActive(int currentMins) {
  for (int i = 0; i < numDoses; i++) {
    if (currentMins >= remindTimes[i] && currentMins <= endTimes[i] && !doseTaken[i]) return true;
  }
  return false;
}

// --- 集成校准算法与正确 LCD 排版的显示刷新函数 ---
void updateDisplay(int vMins) {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= INTERVAL) {
    previousMillis = currentMillis;
    
    int h = vMins / 60;
    int m = vMins % 60;
    
    float rawT = dht.readTemperature();
    float rawH = dht.readHumidity();

    lcd.setCursor(0, 0);
    lcd.print("Time: ");
    if (h < 10) lcd.print("0");
    lcd.print(h);
    lcd.print(":");
    if (m < 10) lcd.print("0");
    lcd.print(m);
    lcd.print("   ");

    lcd.setCursor(0, 1);
    if (isnan(rawT) || isnan(rawH)) {
      lcd.print("DHT Error       ");
    } else {
      // 软校准计算
      float calTemp = rawT + tempOffset;
      float calHumi = constrain(rawH + humiOffset, 0.0, 100.0);

      // 明确输出：左侧为温度 (°C)，右侧为湿度 (%)
      lcd.print(calTemp, 1); lcd.print("C ");
      lcd.print(calHumi, 1); lcd.print("%");
      lcd.print("    "); 
    }
  }
}

void receiveSchedule() {
  static String recvStr = "";
  while (espSerial.available() > 0) {
    char c = espSerial.read();
    if (c == '<') { recvStr = ""; }
    else if (c == '>') { parseDataString(recvStr); }
    else { recvStr += c; }
  }
}

void parseDataString(String data) {
  for (int i = 0; i < 3; i++) {
    startTimes[i] = 0; endTimes[i] = 0; remindTimes[i] = 0;
    pulses[i] = 0; pills[i] = 0; doseTaken[i] = false;
  }
  int commaIndex = data.indexOf(',');
  numDoses = data.substring(0, commaIndex).toInt();
  if (numDoses > 3) numDoses = 3; 
  
  for (int i = 0; i < numDoses; i++) {
    for (int j = 0; j < 5; j++) {
      data = data.substring(commaIndex + 1);
      commaIndex = data.indexOf(',');
      if (commaIndex == -1) commaIndex = data.length();
      int val = data.substring(0, commaIndex).toInt();
      if (j == 0) startTimes[i] = val;
      else if (j == 1) endTimes[i] = val;
      else if (j == 2) remindTimes[i] = val;
      else if (j == 3) pulses[i] = val;
      else if (j == 4) pills[i] = val;
    }
    doseTaken[i] = false; 
  }
  saveToEEPROM();
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Saved to Memory!");
  lcd.setCursor(0, 1); lcd.print("Doses Loaded: "); lcd.print(numDoses);
  delay(1500); lcd.clear();
}

void saveToEEPROM() {
  int addr = 0;
  EEPROM.put(addr, numDoses);     addr += sizeof(numDoses);
  EEPROM.put(addr, startTimes);   addr += sizeof(startTimes);
  EEPROM.put(addr, endTimes);     addr += sizeof(endTimes);
  EEPROM.put(addr, remindTimes);  addr += sizeof(remindTimes);
  EEPROM.put(addr, pulses);       addr += sizeof(pulses);
  EEPROM.put(addr, pills);        addr += sizeof(pills);
}

void loadFromEEPROM() {
  int addr = 0;
  EEPROM.get(addr, numDoses); addr += sizeof(numDoses);
  if (numDoses < 0 || numDoses > 3) { numDoses = 0; return; }
  EEPROM.get(addr, startTimes);   addr += sizeof(startTimes);
  EEPROM.get(addr, endTimes);     addr += sizeof(endTimes);
  EEPROM.get(addr, remindTimes);  addr += sizeof(remindTimes);
  EEPROM.get(addr, pulses);       addr += sizeof(pulses);
  EEPROM.get(addr, pills);        addr += sizeof(pills);

  lcd.clear();
  lcd.print("Loaded Profile!");
  lcd.setCursor(0, 1);
  lcd.print("Doses: "); lcd.print(numDoses);
  delay(1500); lcd.clear();
}

int getActiveDoseIndex(int currentMins) {
  for (int i = 0; i < numDoses; i++) {
    if (currentMins >= startTimes[i] && currentMins <= endTimes[i]) return i;
  }
  return -1;
}

void checkAlarms(int currentMins) {
  for (int i = 0; i < numDoses; i++) {
    if (currentMins > endTimes[i] && !doseTaken[i]) doseTaken[i] = true; 
  }
}

void executeDispensing(int doseIndex) {
  noTone(PIN_BUZZER);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Dispensing...");

  int pillsDropped = 0;
  int targetPills = pills[doseIndex];

  while (pillsDropped < targetPills) {
    bool success = false;
    int attempts = 0;

    while (!success && attempts < 10) { 
      myServo.attach(PIN_SERVO);
      unsigned long collectStart = millis();
      myServo.writeMicroseconds(2400); 
      while (millis() - collectStart < 400) {
        if (digitalRead(PIN_LASER) == LOW) success = true; 
      }
      
      if (!success) {
        for (int pos = 2400; pos >= 500; pos -= 30) {
          myServo.writeMicroseconds(pos);
          if (digitalRead(PIN_LASER) == LOW) { success = true; break; }
          delay(5); 
        }
      }
      
      if (!success) {
        unsigned long pipeSliderWait = millis();
        while (millis() - pipeSliderWait < 600) { 
          if (digitalRead(PIN_LASER) == LOW) { success = true; break; }
        }
      }
      
      myServo.detach(); 

      if (success) {
        pillsDropped++; 
        unsigned long clearWait = millis();
        while (digitalRead(PIN_LASER) == LOW && millis() - clearWait < 200) delay(2); 
      } else {
        attempts++;     
      }
    }

    if (!success) {
      lcd.clear(); lcd.setCursor(0,0); lcd.print("Pill Error!");
      lcd.setCursor(0,1); lcd.print("Press btn to clear");
      tone(PIN_BUZZER, 500);
      while(digitalRead(PIN_MAIN_BUTTON) == LOW) delay(50);
      noTone(PIN_BUZZER); lcd.clear(); lcd.print("Resuming..."); delay(1000);
    }
  }

  lcd.clear(); lcd.print("Adding liquid...");
  
  hallPulseCount = 0;
  int targetPulses = pulses[doseIndex];
  digitalWrite(PIN_MED_PUMP, HIGH);
  
  int lastHallState = digitalRead(PIN_HALL);
  while (hallPulseCount < targetPulses) {
    int currentHallState = digitalRead(PIN_HALL);
    if (currentHallState != lastHallState) {
      if (currentHallState == LOW) hallPulseCount++;
      lastHallState = currentHallState;
    }
  }
  
  digitalWrite(PIN_MED_PUMP, LOW); 
  doseTaken[doseIndex] = true;
  lcd.clear(); lcd.print("Dose Complete!"); delay(3000); lcd.clear();
}