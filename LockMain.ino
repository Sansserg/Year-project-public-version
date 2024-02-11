#include <SoftwareSerial.h>
#include <FPM.h>
#include "SPIFFS.h"
#include <WiFi.h>
#include <dummy.h>
#include <SPI.h>
#include <MFRC522.h>
#include <AmperkaKB.h>
#include <NTPClient.h>
//#include <Servo.h>
#include <FastBot.h> 
#include <PubSubClient.h>


#define WIFI_NETWORK "Sergey"
#define WIFI_PASSWORD "00000000"
#define BOT_API_TOKEN "My_tokey_chat_idmn"
#define CHAT_IDENTIFIER "Mu_chat_id"
FastBot bot(BOT_API_TOKEN);

const char* mqttServer = "192.168.--.---";
const int mqttPort = 1883;
const char* mqttUser = "Sergey";
const char* mqttPassword = "mqtt_password";

const int servoPin = 4;
const int hallSensorPin = 2;
const int keypadDelay = 5000;
unsigned long lastKeyPressTime = 0;  // Variable to store the time of the last key press
String currentPassword = "";
int click_times = 0;

String passwords[100];
String nfcIDs[100];
File eventLogFile;

//Servo MG995_Servo  = Servo();
MFRC522 rfid(5, 32);
AmperkaKB KB(13, 12, 14, 27, 25, 26, 15, 33);
SoftwareSerial fserial(17, 16);
FPM finger(&fserial);
FPM_System_Params params;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);
WiFiClient espClient;
PubSubClient client(espClient);


void setup() {
  rfid.PCD_Init();
  KB.begin(KB4x4);

  Serial.begin(115200);
  fserial.begin(57600);
  SPIFFS.begin(true);
  SPI.begin();

  pinMode(hallSensorPin, INPUT);

  connectWiFi(); // Добавлен вызов функции подключения к WiFi

  readPasswords();
  readNfcIDs();

  timeClient.begin();
  timeClient.setTimeOffset(25200);

  bot.setChatID(CHAT_IDENTIFIER);
  // attach message handler function
  bot.attach(newMsg);

  eventLogFile = SPIFFS.open("/event_log.txt", "a");
  if (!eventLogFile) {
    logEvent("Failed to open event_log.txt for writing", "ERROR");
  }


  client.setServer(mqttServer, mqttPort);
  client.setCallback(callback);
 
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
 
    if (client.connect("ESP32Client", mqttUser, mqttPassword )) {
 
      Serial.println("connected");  
 
    } else {
 
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);
 
    }
  }
 
  client.subscribe("UID_create");
  client.subscribe("enroll_function");
  client.subscribe("pass_create");
  client.subscribe("door_opener");
}

void loop() {
  bot.tick();
  client.loop();
  KB.read();

  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String nfcUID = "";
    for (byte i = 0; i < 4; i++) {
      nfcUID += String(rfid.uid.uidByte[i], DEC);
    }
    Serial.println(nfcUID);
    if (checkNFCUID(nfcUID)) {
      unlockDoor();
    }
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }

  if (KB.justReleased()) {
    char key = KB.getChar;
    //clearPasswordIfTimeout();  // Call the wrapper function

    if (key == '#') {
      if (checkFingerprint()) {
        unlockDoor();}
      }
    else if (key == '*') {
      currentPassword = "";
      click_times = 0;
    } else {
    Serial.println(key);

      click_times++;
      currentPassword += key;
      Serial.println(currentPassword);
      if (click_times == 4) {
        if (checkPassword(currentPassword)) {
          unlockDoor();
          logEvent("Keypad unlocked door", "INF");
        }
        currentPassword = "";
        click_times = 0;
      }
    }

    lastKeyPressTime = millis();  // Update the last key press time
  }
}

void callback(String topic, byte* payload, unsigned int length) {
 
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
 
  Serial.print("Message:");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
 
  Serial.println();
  Serial.println("-----------------------");
 
  if (topic == "door_opener") {
    unlockDoor();
  }
  else if (topic == "enroll_function") {
    int16_t fid;
    if (get_free_id(&fid))
        enroll_finger(fid);
    else
        Serial.println("No free slot in flash library!");
  }
  else if (topic == "pass_create") 
    addNewPassword();
  else if (topic == "UID_create")
    addNewNfcTag();
  
}

bool checkNFCUID(String uid) {
  for (int i = 0; i < 100; i++) {
    if (uid == nfcIDs[i]) {
      logEvent("NFC_ID unlocked door", "INF");
      return true;
    }
  }
  return false;
}

int checkFingerprint(void)  {
    int16_t p = -1;

    /* first get the finger image */
    Serial.println("Waiting for valid finger");
    while (p != FPM_OK) {
        p = finger.getImage();
        switch (p) {
            case FPM_OK:
                Serial.println("Image taken");
                break;
            case FPM_NOFINGER:
                Serial.println(".");
                break;
            case FPM_PACKETRECIEVEERR:
                Serial.println("Communication error");
                break;
            case FPM_IMAGEFAIL:
                Serial.println("Imaging error");
                break;
            case FPM_TIMEOUT:
                Serial.println("Timeout!");
                break;
            case FPM_READ_ERROR:
                Serial.println("Got wrong PID or length!");
                break;
            default:
                Serial.println("Unknown error");
                break;
        }
        yield();
    }

    /* convert it */
    p = finger.image2Tz();
    switch (p) {
        case FPM_OK:
            Serial.println("Image converted");
            break;
        case FPM_IMAGEMESS:
            Serial.println("Image too messy");
            return p;
        case FPM_PACKETRECIEVEERR:
            Serial.println("Communication error");
            return p;
        case FPM_FEATUREFAIL:
            Serial.println("Could not find fingerprint features");
            return p;
        case FPM_INVALIDIMAGE:
            Serial.println("Could not find fingerprint features");
            return p;
        case FPM_TIMEOUT:
            Serial.println("Timeout!");
            return p;
        case FPM_READ_ERROR:
            Serial.println("Got wrong PID or length!");
            return p;
        default:
            Serial.println("Unknown error");
            return p;
    }

    /* search the database for the converted print */
    uint16_t fid, score;
    p = finger.searchDatabase(&fid, &score);
    
    /* now wait to remove the finger, though not necessary; 
       this was moved here after the search because of the R503 sensor, 
       which seems to wipe its buffers after each scan */
    Serial.println("Remove finger");
    while (finger.getImage() != FPM_NOFINGER) {
        delay(500);
    }
    Serial.println();
    
    if (p == FPM_OK) {
        Serial.println("Found a print match!");
    } else if (p == FPM_PACKETRECIEVEERR) {
        Serial.println("Communication error");
        return p;
    } else if (p == FPM_NOTFOUND) {
        Serial.println("Did not find a match");
        return p;
    } else if (p == FPM_TIMEOUT) {
        Serial.println("Timeout!");
        return p;
    } else if (p == FPM_READ_ERROR) {
        Serial.println("Got wrong PID or length!");
        return p;
    } else {
        Serial.println("Unknown error");
        return p;
    }

    // found a match!
    Serial.print("Found ID #"); Serial.print(fid);
    Serial.print(" with confidence of "); Serial.println(score);
}

int16_t enroll_finger(int16_t fid) {
    int16_t p = -1;
    Serial.println("Waiting for valid finger to enroll");
    while (p != FPM_OK) {
        p = finger.getImage();
        switch (p) {
            case FPM_OK:
                Serial.println("Image taken");
                break;
            case FPM_NOFINGER:
                Serial.println(".");
                break;
            case FPM_PACKETRECIEVEERR:
                Serial.println("Communication error");
                break;
            case FPM_IMAGEFAIL:
                Serial.println("Imaging error");
                break;
            case FPM_TIMEOUT:
                Serial.println("Timeout!");
                break;
            case FPM_READ_ERROR:
                Serial.println("Got wrong PID or length!");
                break;
            default:
                Serial.println("Unknown error");
                break;
        }
        yield();
    }
    // OK success!

    p = finger.image2Tz(1);
    switch (p) {
        case FPM_OK:
            Serial.println("Image converted");
            break;
        case FPM_IMAGEMESS:
            Serial.println("Image too messy");
            return p;
        case FPM_PACKETRECIEVEERR:
            Serial.println("Communication error");
            return p;
        case FPM_FEATUREFAIL:
            Serial.println("Could not find fingerprint features");
            return p;
        case FPM_INVALIDIMAGE:
            Serial.println("Could not find fingerprint features");
            return p;
        case FPM_TIMEOUT:
            Serial.println("Timeout!");
            return p;
        case FPM_READ_ERROR:
            Serial.println("Got wrong PID or length!");
            return p;
        default:
            Serial.println("Unknown error");
            return p;
    }

    Serial.println("Remove finger");
    delay(2000);
    p = 0;
    while (p != FPM_NOFINGER) {
        p = finger.getImage();
        yield();
    }

    p = -1;
    Serial.println("Place same finger again");
    while (p != FPM_OK) {
        p = finger.getImage();
        switch (p) {
            case FPM_OK:
                Serial.println("Image taken");
                break;
            case FPM_NOFINGER:
                Serial.print(".");
                break;
            case FPM_PACKETRECIEVEERR:
                Serial.println("Communication error");
                break;
            case FPM_IMAGEFAIL:
                Serial.println("Imaging error");
                break;
            case FPM_TIMEOUT:
                Serial.println("Timeout!");
                break;
            case FPM_READ_ERROR:
                Serial.println("Got wrong PID or length!");
                break;
            default:
                Serial.println("Unknown error");
                break;
        }
        yield();
    }

    // OK success!

    p = finger.image2Tz(2);
    switch (p) {
        case FPM_OK:
            Serial.println("Image converted");
            break;
        case FPM_IMAGEMESS:
            Serial.println("Image too messy");
            return p;
        case FPM_PACKETRECIEVEERR:
            Serial.println("Communication error");
            return p;
        case FPM_FEATUREFAIL:
            Serial.println("Could not find fingerprint features");
            return p;
        case FPM_INVALIDIMAGE:
            Serial.println("Could not find fingerprint features");
            return p;
        case FPM_TIMEOUT:
            Serial.println("Timeout!");
            return false;
        case FPM_READ_ERROR:
            Serial.println("Got wrong PID or length!");
            return false;
        default:
            Serial.println("Unknown error");
            return p;
    }


    // OK converted!
    p = finger.createModel();
    if (p == FPM_OK) {
        Serial.println("Prints matched!");
    } else if (p == FPM_PACKETRECIEVEERR) {
        Serial.println("Communication error");
        return p;
    } else if (p == FPM_ENROLLMISMATCH) {
        Serial.println("Fingerprints did not match");
        return p;
    } else if (p == FPM_TIMEOUT) {
        Serial.println("Timeout!");
        return p;
    } else if (p == FPM_READ_ERROR) {
        Serial.println("Got wrong PID or length!");
        return p;
    } else {
        Serial.println("Unknown error");
        return p;
    }

    Serial.print("ID "); Serial.println(fid);
    p = finger.storeModel(fid);
    if (p == FPM_OK) {
        Serial.println("Stored!");
        return 0;
    } else if (p == FPM_PACKETRECIEVEERR) {
        Serial.println("Communication error");
        return p;
    } else if (p == FPM_BADLOCATION) {
        Serial.println("Could not store in that location");
        return p;
    } else if (p == FPM_FLASHERR) {
        Serial.println("Error writing to flash");
        return p;
    } else if (p == FPM_TIMEOUT) {
        Serial.println("Timeout!");
        return p;
    } else if (p == FPM_READ_ERROR) {
        Serial.println("Got wrong PID or length!");
        return p;
    } else {
        Serial.println("Unknown error");
        return p;
    }
}

bool get_free_id(int16_t * fid) {
    int16_t p = -1;
    for (int page = 0; page < (params.capacity / FPM_TEMPLATES_PER_PAGE) + 1; page++) {
        p = finger.getFreeIndex(page, fid);
        switch (p) {
            case FPM_OK:
                if (*fid != FPM_NOFREEINDEX) {
                    Serial.print("Free slot at ID ");
                    Serial.println(*fid);
                    return true;
                }
                break;
            case FPM_PACKETRECIEVEERR:
                Serial.println("Communication error!");
                return false;
            case FPM_TIMEOUT:
                Serial.println("Timeout!");
                return false;
            case FPM_READ_ERROR:
                Serial.println("Got wrong PID or length!");
                return false;
            default:
                Serial.println("Unknown error!");
                return false;
        }
        yield();
    }
    
    Serial.println("No free slots!");
    return false;
}

bool checkPassword(String password) {
  click_times = 0;
  currentPassword = "";
  bool passwordFound = false;
  for (int i = 0; i < 100; i++) {
    Serial.println(passwords[i]);
    if (password == passwords[i]) {
      passwordFound = true;
      break;
    }
  }
  Serial.print(currentPassword);
  return passwordFound;
}

void unlockDoor() {
  Serial.println("Door opened");
//  MG995_Servo.write(4, 170);
  logEvent("Door unlocked", "INF");
  while (digitalRead(hallSensorPin) != LOW) {
  }
    lockDoor();

}

void lockDoor() {
  Serial.println("Door locked");

//  MG995_Servo.write(4, 0);
  logEvent("Door locked", "INF");
}

void readPasswords() {
  File file = SPIFFS.open("/passwords.txt", "r");
  if (!file) {
    logEvent("Failed to open passwords.txt", "ERROR");
    return;
  }

  int index = 0;
  while (file.available()) {
    String password = file.readStringUntil('\n');
    password.trim();
    passwords[index] = password;
    index++;
  }

  file.close();

  for (int i = 0; i < index; i++) {
    Serial.println(passwords[i]);
  }
}
/*
void clearPasswordIfTimeout() {
  unsigned long currentTime = millis();
  if ((currentTime - lastKeyPressTime) > 5000) {  // Check if more than 5 seconds have passed
    currentPassword = "";  // Clear the password
    click_times = 0;       // Reset click_times
  }
}
*/

void readNfcIDs() {
  File file = SPIFFS.open("/nfc_ids.txt", "r");
  if (!file) {
    logEvent("Failed to open nfc_ids.txt", "ERROR");
    return;
  }

  int index = 0;
  while (file.available() && index < 100) {
    String nfcID = file.readStringUntil('\n');
    nfcID.trim();
    nfcIDs[index] = nfcID;
    index++;
  }

  file.close();

  for (int i = 0; i < index; i++) {
    Serial.println(nfcIDs[i]);
  }
}

void addNewNfcTag() {
  Serial.println("addNewNfcTag");
  while (true){
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String newTagUID = "";
      for (byte i = 0; i < 4; i++) {
        newTagUID += String(rfid.uid.uidByte[i], DEC);
      }
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      
      // Проверяем, есть ли такая метка уже в списке
      bool tagExists = false;
      for (int i = 0; i < 100; i++) {
        if (newTagUID == nfcIDs[i]) {
          tagExists = true;
          break;
        }
      }

      if (!tagExists) {
        // Сохраняем новую метку в файл
        File nfcFile = SPIFFS.open("/nfc_ids.txt", "a");
        if (nfcFile) {
          nfcFile.println(newTagUID);
          nfcFile.close();
          // Обновляем массив UID меток в программе
          for (int i = 0; i < 100; i++) {
            if (nfcIDs[i] == "") {
              nfcIDs[i] = newTagUID;
              break;
            }
          }
          Serial.println("New NFC tag added and updated successfully.");
          break;
        } else {
          Serial.println("Error opening NFC tag file for writing.");
          break;

        }
      } else {
        Serial.println("NFC tag already exists in the list.");
        break;

      }
    }
  }
}

void addNewPassword() {
  Serial.println("addNewPassword");
  int indx = 0;
  String newPassword = "";
  while(indx != 4) {
    KB.read();

    if (KB.justReleased()) {
      char key = KB.getChar;
      newPassword += key;
      indx++;
    }
  }

  bool passExists = false;
  for (int i = 0; i < 100; i++) {
    if (newPassword == passwords[i]) {
      passExists = true;
      break;
    }
  }

  if (!passExists) {
    // Сохраняем новую метку в файл
    File passFile = SPIFFS.open("/passwords.txt", "a");
    if (passFile) {
      passFile.println(newPassword);
      passFile.close();
      // Обновляем массив UID меток в программе
      for (int i = 0; i < 100; i++) {
        if (passwords[i] == "") {
          passwords[i] = newPassword;
          break;
        }
      }
      Serial.println("New password added and updated successfully.");
    } else {
      Serial.println("Error opening password file for writing.");
    }
  } else {
    Serial.println("password already exists in the list.");
  }
}


void logEvent(String message, String type) {
  String logEntry = "[" + type + "] " + getTime() + " " + message + "\n";
  Serial.println(logEntry);
  eventLogFile.println(logEntry);
}

String getTime() {  
  timeClient.update();
  return timeClient.getFormattedTime();
}


void connectWiFi() {
  delay(2000);
  Serial.begin(115200);
  Serial.println();

  WiFi.begin(WIFI_NETWORK, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() > 15000) ESP.restart();
  }
  Serial.println("Connected");
}

void newMsg(FB_msg& msg) {
  if(msg.text == "Open") unlockDoor();
  else if(msg.text == "Logs"){
    bot.sendFile(eventLogFile, FB_DOC, "event_log.txt",  CHAT_IDENTIFIER);
  } 
  
  else Serial.println(msg.text);
}
