/*
 * Pilot TV przez internet - ESP32 + dioda IR
 * Dekoder ADB: kody uczone z oryginalnego pilota (odbiornik VS1838B).
 * Telewizor Sony KDL (Bravia): kody wbudowane (Sony SIRC 12-bit, adres 1), klawisze z prefiksem tv_.
 *
 * Komunikacja: MQTT (np. HiveMQ Cloud), dziala zza domu bez przekierowania portow.
 *   pilottv/<id>/cmd     <- polecenia tekstowe (strona www wysyla tutaj)
 *   pilottv/<id>/event   -> potwierdzenia: {"ev":"sent","key":"vol_up"} itd.
 *   pilottv/<id>/info    -> (retained) IP, RSSI, lista nauczonych klawiszy
 *   pilottv/<id>/online  -> (retained) "1" / "0" (LWT)
 *
 * Polecenia:
 *   <klawisz>         - dekoder: power, vol_up, ch_up, d0..d9, ok, red... (dowolna nazwa [a-z0-9_]
 *                       do 13 znakow) - dziala po nauczeniu
 *   tv_on, tv_off, tv_power, tv_vol_up, tv_vol_down, tv_mute, tv_input... - telewizor Sony (wbudowane)
 *   ch:123            - wybierz kanal 123 na dekoderze (d1, d2, d3 po kolei)
 *   learn:<klawisz>   - naucz klawisz z oryginalnego pilota (15 s na nacisniecie)
 *   learn_cancel      - przerwij nauke
 *   forget:<klawisz>  - usun nauczony kod (dla tv_ wraca kod wbudowany Sony)
 *   forget_all        - usun wszystkie nauczone kody
 *
 * Biblioteki: IRremote (Armin Joachimsmeyer) 4.x, PubSubClient 2.8
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include "config.h"
#include <IRremote.hpp>

#define FW_VERSION      "1.1"
#define SONY_ADDRESS    1           // Sony TV
#define SONY_REPEATS    2           // Sony wymaga min. 3 ramek (1 + 2 powtorzenia)
#define RAW_MAX         200
#define LEARN_TIMEOUT   15000
#define DIGIT_GAP_MS    350

// ---------- Wbudowane kody Sony Bravia (SIRC 12-bit) ----------
struct SonyKey { const char *name; uint8_t cmd; };
const SonyKey SONY_KEYS[] = {
  {"tv_power", 0x15}, {"tv_on", 0x2E}, {"tv_off", 0x2F},
  {"tv_vol_up", 0x12}, {"tv_vol_down", 0x13}, {"tv_mute", 0x14},
  {"tv_ch_up", 0x10}, {"tv_ch_down", 0x11}, {"tv_input", 0x25},
  {"tv_d1", 0x00}, {"tv_d2", 0x01}, {"tv_d3", 0x02}, {"tv_d4", 0x03}, {"tv_d5", 0x04},
  {"tv_d6", 0x05}, {"tv_d7", 0x06}, {"tv_d8", 0x07}, {"tv_d9", 0x08}, {"tv_d0", 0x09},
  {"tv_up", 0x74}, {"tv_down", 0x75}, {"tv_left", 0x34}, {"tv_right", 0x33},
  {"tv_ok", 0x65}, {"tv_home", 0x60}, {"tv_back", 0x23},
};

// ---------- Nauczone kody (NVS) ----------
struct Learned {
  uint8_t  protocol;
  uint8_t  bits;
  uint16_t address;
  uint16_t command;
  uint16_t len;                     // liczba czasow raw (0 = brak)
  uint16_t us[RAW_MAX];
};

WiFiClientSecure net;
PubSubClient mqtt(net);
Preferences prefs;

String topicCmd, topicEvent, topicInfo, topicOnline;
String learnKey;
uint32_t learnDeadline = 0;
uint32_t lastMqttTry = 0;

// ---------- Pomocnicze ----------
bool validKey(const String &k) {
  if (k.length() == 0 || k.length() > 13) return false;   // klucz NVS max 15 znakow ("k_" + 13)
  for (char c : k) if (!isLowerCase(c) && !isDigit(c) && c != '_') return false;
  return true;
}

void publishEvent(const String &json) {
  Serial.println(json);
  if (mqtt.connected()) mqtt.publish(topicEvent.c_str(), json.c_str());
}

void publishInfo() {
  if (!mqtt.connected()) return;
  String list = prefs.getString("list", "");
  String learned = "[";
  int start = 0;
  while (start < (int)list.length()) {
    int comma = list.indexOf(',', start);
    if (comma < 0) comma = list.length();
    if (learned.length() > 1) learned += ',';
    learned += "\"" + list.substring(start, comma) + "\"";
    start = comma + 1;
  }
  learned += ']';
  String json = "{\"ver\":\"" FW_VERSION "\",\"ip\":\"" + WiFi.localIP().toString() +
                "\",\"rssi\":" + WiFi.RSSI() + ",\"learned\":" + learned + "}";
  mqtt.publish(topicInfo.c_str(), json.c_str(), true);
}

void listAdd(const String &k) {
  String list = "," + prefs.getString("list", "") + ",";
  if (list.indexOf("," + k + ",") >= 0) return;
  String cur = prefs.getString("list", "");
  prefs.putString("list", cur.length() ? cur + "," + k : k);
}

void listRemove(const String &k) {
  String list = "," + prefs.getString("list", "") + ",";
  list.replace("," + k + ",", ",");
  list = list.substring(1, list.length() - 1);
  prefs.putString("list", list);
}

// ---------- Wysylanie IR ----------
bool sendLearned(const String &k) {
  String nvsKey = "k_" + k;
  if (!prefs.isKey(nvsKey.c_str())) return false;
  static Learned l;
  memset(&l, 0, sizeof(l));
  prefs.getBytes(nvsKey.c_str(), &l, sizeof(l));

  IRData d = {};
  d.protocol = (decode_type_t)l.protocol;
  d.address = l.address;
  d.command = l.command;
  d.numberOfBits = l.bits;
  int repeats = (d.protocol == SONY) ? SONY_REPEATS : 0;
  if (d.protocol != UNKNOWN && IrSender.write(&d, repeats)) return true;
  if (l.len > 0) {                 // protokol nieobslugiwany przez write() -> surowe czasy
    IrSender.sendRaw(l.us, l.len, 38);
    return true;
  }
  return false;
}

bool sendKey(const String &k) {
  if (sendLearned(k)) return true;
  for (const SonyKey &s : SONY_KEYS) {
    if (k == s.name) {
      IrSender.sendSony(SONY_ADDRESS, s.cmd, SONY_REPEATS, SIRCS_12_PROTOCOL);
      return true;
    }
  }
  return false;
}

// ---------- Nauka ----------
void startLearn(const String &k) {
  if (PIN_IR_RECV < 0) { publishEvent("{\"ev\":\"error\",\"msg\":\"brak odbiornika IR\"}"); return; }
  learnKey = k;
  learnDeadline = millis() + LEARN_TIMEOUT;
  IrReceiver.begin(PIN_IR_RECV, false);
  publishEvent("{\"ev\":\"learning\",\"key\":\"" + k + "\"}");
}

void stopLearn() {
  IrReceiver.stop();
  learnKey = "";
}

void handleLearn() {
  if (learnKey.isEmpty()) return;
  if ((int32_t)(millis() - learnDeadline) > 0) {
    publishEvent("{\"ev\":\"learn_timeout\",\"key\":\"" + learnKey + "\"}");
    stopLearn();
    return;
  }
  if (!IrReceiver.decode()) return;

  IRData &d = IrReceiver.decodedIRData;
  uint16_t rawlen = IrReceiver.irparams.rawlen;
  if ((d.flags & (IRDATA_FLAGS_IS_REPEAT | IRDATA_FLAGS_WAS_OVERFLOW)) || rawlen < 8) {
    IrReceiver.resume();           // powtorzenie / smiec - czekamy na pelna ramke
    return;
  }

  static Learned l;
  memset(&l, 0, sizeof(l));
  l.protocol = d.protocol;
  l.bits = d.numberOfBits;
  l.address = d.address;
  l.command = d.command;
  // rawbuf[0] jest pusty; dalej na przemian mark/space w tickach po 50 us
  l.len = min<uint16_t>(rawlen - 1, RAW_MAX);
  for (uint16_t i = 0; i < l.len; i++) {
    int32_t t = (int32_t)IrReceiver.irparams.rawbuf[i + 1] * MICROS_PER_TICK;
    t += (i % 2 == 0) ? -MARK_EXCESS_MICROS : MARK_EXCESS_MICROS;
    l.us[i] = constrain(t, 1, 65535);
  }
  String proto = String(getProtocolString(d.protocol));

  String nvsKey = "k_" + learnKey;
  prefs.putBytes(nvsKey.c_str(), &l, offsetof(Learned, us) + l.len * sizeof(uint16_t));
  listAdd(learnKey);
  publishEvent("{\"ev\":\"learned\",\"key\":\"" + learnKey + "\",\"proto\":\"" + proto +
               "\",\"addr\":" + d.address + ",\"cmd\":" + d.command + "}");
  stopLearn();
  publishInfo();
}

// ---------- Polecenia ----------
void handleCommand(String c) {
  c.trim();
  c.toLowerCase();

  if (c.startsWith("learn:")) {
    String k = c.substring(6);
    if (!validKey(k)) { publishEvent("{\"ev\":\"error\",\"msg\":\"zla nazwa klawisza\"}"); return; }
    startLearn(k);
    return;
  }
  if (c == "learn_cancel") { stopLearn(); publishEvent("{\"ev\":\"learn_cancel\"}"); return; }
  if (c.startsWith("forget:")) {
    String k = c.substring(7);
    if (validKey(k)) { prefs.remove(("k_" + k).c_str()); listRemove(k); }
    publishEvent("{\"ev\":\"forgot\",\"key\":\"" + k + "\"}");
    publishInfo();
    return;
  }
  if (c == "forget_all") {
    prefs.clear();
    publishEvent("{\"ev\":\"forgot_all\"}");
    publishInfo();
    return;
  }
  if (c.startsWith("ch:")) {
    String num = c.substring(3);
    if (num.length() == 0 || num.length() > 4) return;
    for (char ch : num) {
      if (!isDigit(ch)) return;
    }
    for (char ch : num) {
      sendKey(String("d") + ch);
      delay(DIGIT_GAP_MS);
    }
    publishEvent("{\"ev\":\"sent\",\"key\":\"" + c + "\"}");
    return;
  }

  if (!learnKey.isEmpty()) stopLearn();   // wyslanie przerywa nauke
  if (sendKey(c)) publishEvent("{\"ev\":\"sent\",\"key\":\"" + c + "\"}");
  else            publishEvent("{\"ev\":\"error\",\"msg\":\"klawisz " + c + " nienauczony\"}");
}

void onMqtt(char *topic, byte *payload, unsigned int len) {
  String msg;
  msg.reserve(len);
  for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  handleCommand(msg);
}

// ---------- Siec ----------
void mqttReconnect() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastMqttTry < 5000) return;
  lastMqttTry = millis();

  String clientId = "pilottv-" DEVICE_ID "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.print("MQTT laczenie... ");
  if (mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS, topicOnline.c_str(), 1, true, "0")) {
    Serial.println("OK");
    mqtt.publish(topicOnline.c_str(), "1", true);
    mqtt.subscribe(topicCmd.c_str());
    publishInfo();
  } else {
    Serial.printf("blad %d\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nPilot TV " FW_VERSION);

  prefs.begin("pilottv", false);
  IrSender.begin(PIN_IR_SEND);

  String base = "pilottv/" DEVICE_ID "/";
  topicCmd = base + "cmd";
  topicEvent = base + "event";
  topicInfo = base + "info";
  topicOnline = base + "online";

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);            // mniejsze opoznienie reakcji na polecenia
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) { delay(500); Serial.print('.'); }
  Serial.println(WiFi.status() == WL_CONNECTED ? String(" OK ") + WiFi.localIP().toString() : String(" brak (proba w tle)"));

  net.setInsecure();               // szyfrowanie TLS bez weryfikacji certyfikatu brokera
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onMqtt);
  mqtt.setBufferSize(1024);
  mqtt.setKeepAlive(30);
}

void loop() {
  mqttReconnect();
  mqtt.loop();
  handleLearn();

  // Polecenia tez z portu szeregowego - wygodne do testow bez internetu
  if (Serial.available()) handleCommand(Serial.readStringUntil('\n'));
}
