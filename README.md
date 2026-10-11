# Pilot TV – sterowanie dekoderem i telewizorem przez internet

ESP32 z diodą IR stoi przy telewizorze i jest połączony z domowym WiFi. Telefon (strona www / aplikacja PWA)
wysyła polecenia przez broker MQTT w chmurze. Działa z każdego miejsca i nie wymaga przekierowania portów
na routerze.

```
Telefon (strona)  ──wss:8884──►  HiveMQ Cloud  ◄──mqtts:8883──  ESP32 ──► dioda IR ──► dekoder ADB + Sony KDL
```

- **Dekoder ADB** (kanały, głośność, menu): kody pilota ESP32 uczy się z oryginalnego pilota przez
  odbiornik IR. Na stronie jest kreator, który prowadzi przez wszystkie przyciski.
- **Telewizor Sony KDL**: kody Sony Bravia (SIRC 12-bit) są wbudowane, więc WŁ/WYŁ, źródło i głośność
  telewizora działają od razu.

## Części

| Część | Uwagi |
|---|---|
| ESP32-C3 SuperMini albo ESP32 DevKit | dowolny ESP32 |
| Dioda IR 940 nm (TSAL6200, LD271 lub podobna) | 1–2 szt. |
| Tranzystor NPN S8050 / 2N2222 / BC337 | dioda pobiera ok. 100 mA, pin GPIO tyle nie da |
| Rezystor 33 Ω (dla 1 diody) albo 22 Ω (2 diody szeregowo) | ogranicza prąd diody |
| Rezystor 1 kΩ | baza tranzystora |
| Odbiornik IR VS1838B / TSOP38238 | **potrzebny**: do nauki kodów pilota dekodera |

## Schemat

```
 5V ──[33Ω]──►|── dioda IR ──┐            (anoda ►| katoda)
                              │ C
 GPIO4 ──[1kΩ]────────── B  NPN
                              │ E
 GND ─────────────────────────┘

 VS1838B:  OUT → GPIO5,  VCC → 3V3,  GND → GND
 (VS1838B patrząc na kopułkę, od lewej: OUT, GND, VCC)
```

Diodę skieruj tak, żeby sygnał doszedł i do dekodera, i do telewizora. Może też świecić w sufit lub
ścianę, bo IR dobrze się odbija. Najpewniej działają 2 diody: jedna na dekoder, druga na telewizor.
Zasilanie z ładowarki USB 5V.

## Krok 1: broker MQTT (darmowy HiveMQ Cloud)

1. Załóż konto na https://console.hivemq.cloud i utwórz klaster **Serverless (Free)**.
2. **Access Management** → dodaj login i hasło z uprawnieniem *Publish and Subscribe*.
3. Skopiuj **Cluster URL** (np. `abc123.s1.eu.hivemq.cloud`).

## Krok 2: firmware ESP32

1. Arduino IDE: płytka **ESP32C3 Dev Module** (dla C3 SuperMini włącz *USB CDC On Boot: Enabled*)
   albo **ESP32 Dev Module**.
2. Biblioteki: **IRremote** (Armin Joachimsmeyer, 4.x) i **PubSubClient** (Nick O'Leary).
3. W `firmware/PilotTV/` skopiuj `config.example.h` jako `config.h` i wpisz WiFi, dane z HiveMQ
   oraz `DEVICE_ID` (np. `salon`).
4. Wgraj. Na monitorze portu (115200) zobaczysz `WiFi OK` i `MQTT laczenie... OK`.
5. Test bez internetu: w monitorze portu wpisz `vol_up` i naciśnij Enter.

## Krok 3: strona / aplikacja na telefon

Strona (folder `docs/`) jest opublikowana na GitHub Pages: **https://guldorf.github.io/pilot-tv/**

1. Otwórz stronę w Chrome na Androidzie.
2. ⚙ → wpisz host z HiveMQ, port **8884**, login, hasło, nazwę urządzenia. Dane zostają tylko w telefonie.
3. Menu Chrome → **Dodaj do ekranu głównego**. Pilot otwiera się wtedy jak zwykła aplikacja.

Zielona kropka oznacza, że ESP32 jest online.

## Krok 4: nauka pilota dekodera (jednorazowo, w domu)

1. Na stronie dotknij **Naucz pilota dekodera krok po kroku**.
2. Strona pokazuje po kolei nazwy przycisków (POWER, MENU, VOL +, cyfry…). Każdy z nich naciśnij na
   pilocie ADB skierowanym na odbiornik VS1838B, z odległości 5–30 cm.
3. Przycisk, którego nie potrzebujesz, pomiń. Nienauczone przyciski są na stronie wyszarzone.

Pojedynczy przycisk nauczysz od nowa tak: zaznacz **Naucz pojedynczy przycisk**, dotknij go na stronie
i naciśnij na pilocie.

## Obsługa

- **Telewizor Sony: WŁ / WYŁ** to osobne kody „Power On” i „Power Off”. Zza domu nie widzisz
  telewizora, więc przełącznik działałby na ślepo, a te kody zawsze ustawiają właściwy stan.
- **Dekoder ⏻**: pilot ADB ma tylko przełącznik, więc tu przełączasz na ślepo.
- Głośność, kanały i strzałki można przytrzymać, wtedy polecenie się powtarza.
- Cyfry: wpisz numer kanału. Po 1,2 s bez kolejnej cyfry pilot wysyła cały numer.

## Polecenia MQTT (`pilottv/<id>/cmd`)

- dekoder (po nauczeniu): `power menu epg info red green yellow blue vol_up vol_down mute list ch_up ch_down d0…d9 home up back left ok right exit down`
- telewizor Sony: `tv_on tv_off tv_power tv_input tv_vol_up tv_vol_down tv_mute` (oraz `tv_ch_up`, `tv_d1`, `tv_ok`…)
- `ch:123`, `learn:<klawisz>`, `learn_cancel`, `forget:<klawisz>`, `forget_all`

Wysłać je może cokolwiek, co obsługuje MQTT, np. Home Assistant albo aplikacja *IoT MQTT Panel*.

## Uwagi

- Połączenie ESP32 z brokerem jest szyfrowane (TLS), ale firmware nie weryfikuje certyfikatu
  brokera (`setInsecure`).
- Pilot nie wie, czy telewizor jest włączony, bo IR działa tylko w jedną stronę. Potwierdzenie
  „wysłano” znaczy tylko, że dioda błysnęła.
