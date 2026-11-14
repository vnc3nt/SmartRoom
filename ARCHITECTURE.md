
     ╔════════════════════════════════════════════════════════════╗
     ║                                                            ║
     ║            🏠 SmartRoom - Matter IR Controller             ║
     ║                                                            ║
     ║              HomeKit ←→ ESP32-C6 ←→ IR LED                ║
     ║                                                            ║
     ╚════════════════════════════════════════════════════════════╝


┌─────────────────────────────────────────────────────────────────┐
│                        📱 HomeKit (iPhone)                      │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                                                          │  │
│  │              [  IR Button  ]                             │  │
│  │                                                          │  │
│  │              Tap to send IR signal                       │  │
│  │                                                          │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         │ WiFi / Matter Protocol
                         │ (Commissioning Code: 20202021)
                         │
┌────────────────────────▼────────────────────────────────────────┐
│                     💻 ESP32-C6 DevKit                          │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Matter Stack                                            │  │
│  │  ├─ WiFi Manager                                         │  │
│  │  ├─ BLE Commissioning                                    │  │
│  │  ├─ mDNS / Service Discovery                             │  │
│  │  └─ On/Off Cluster (Button)                              │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  Application Layer (smart.c)                             │  │
│  │  ├─ app_attribute_update_cb()  ← Button pressed          │  │
│  │  ├─ send_ir_signal()           ← Send IR via RMT         │  │
│  │  └─ app_event_cb()             ← Matter events           │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  IR Transmitter (RMT Driver)                             │  │
│  │  ├─ NEC Encoder                                          │  │
│  │  ├─ 38kHz Carrier                                        │  │
│  │  └─ GPIO 18 Output                                       │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         │ GPIO 18 (with 330Ω resistor)
                         │
┌────────────────────────▼────────────────────────────────────────┐
│                      💡 IR LED (IR333C)                         │
│                                                                 │
│              ◄──────  38kHz Modulated Signal  ──────►          │
│                                                                 │
│              Address: 0xFF00  |  Command: 0xBA45               │
│                                                                 │
└────────────────────────┬────────────────────────────────────────┘
                         │
                         │ IR Light (940nm)
                         │
                         ▼
              ┌─────────────────────┐
              │                     │
              │   🖥️ TV / Device    │
              │                     │
              │   (receives signal) │
              │                     │
              └─────────────────────┘


═══════════════════════════════════════════════════════════════════

                        📂 File Structure

SmartRoom/
│
├── 🔧 Build & Config Files
│   ├── CMakeLists.txt              (Project config)
│   ├── sdkconfig.defaults          (Default settings)
│   ├── partitions.csv              (Flash layout)
│   └── idf_component.yaml          (Dependencies)
│
├── 📜 Scripts
│   ├── build.sh                    (Build automation)
│   └── setup_wifi.sh               (WiFi configuration)
│
├── 📖 Documentation
│   ├── QUICKSTART.md               (5-step guide)
│   ├── README_SETUP.md             (Full documentation)
│   ├── CHANGELOG.md                (All changes)
│   └── ARCHITECTURE.md             (This file)
│
├── 🗂️ main/
│   ├── smart.c                     (Main application)
│   ├── ir_nec_encoder.c            (IR encoder)
│   ├── ir_nec_encoder.h            (IR header)
│   ├── CMakeLists.txt              (Component config)
│   └── idf_component.yml           (Component deps)
│
├── 🔨 build/                       (Compiled binaries)
│   ├── SmartRoom.bin
│   ├── bootloader/
│   └── partition_table/
│
└── 📦 managed_components/          (External libs)
    └── espressif__esp_matter/

═══════════════════════════════════════════════════════════════════

                      🔄 Data Flow Diagram

    [HomeKit App]
         │
         │ (1) User taps button
         │
         ▼
    [Matter Stack]
         │
         │ (2) OnOff attribute change
         │
         ▼
    [app_attribute_update_cb]
         │
         │ (3) Detects button press
         │
         ▼
    [send_ir_signal]
         │
         │ (4) Prepare NEC frame
         │     Address: 0xFF00
         │     Command: 0xBA45
         │
         ▼
    [RMT TX Channel]
         │
         │ (5) Encode with 38kHz carrier
         │
         ▼
    [GPIO 18]
         │
         │ (6) PWM signal output
         │
         ▼
    [IR LED]
         │
         │ (7) Infrared light (940nm)
         │
         ▼
    [Target Device] → 🎬 Action executed!

═══════════════════════════════════════════════════════════════════

                      ⚡ Power & Timing

GPIO 18 Signal (38kHz carrier):
    
    ┌─┐ ┌─┐ ┌─┐     ┌─┐ ┌─┐ ┌─┐
    │ │ │ │ │ │ ... │ │ │ │ │ │
    ┘ └─┘ └─┘ └─    ┘ └─┘ └─┘ └─
    
    Period: 26.3 µs (38 kHz)
    Duty:   33% (8.8 µs high, 17.5 µs low)

NEC Protocol Frame:
    
    |─────────── ~68ms total ─────────|
    
    ┌────┐        ┌──────────────────────────────────┐
    │AGC │  Space │ Data (32 bits)                   │
    └────┘        └──────────────────────────────────┘
    9ms   4.5ms    Address (16b) + Command (16b)

Button Response Time:
    
    HomeKit Tap → Matter → ESP32 → IR LED
    └─────────────── ~50-100ms ──────────────┘

═══════════════════════════════════════════════════════════════════

                    🔐 Security Architecture

[HomeKit App]
     │
     │ Encrypted (Matter TLS)
     │ - Device Attestation
     │ - Secure Session
     ▼
[Matter Stack on ESP32]
     │
     ├─ DAC (Device Attestation Certificate)
     │  └─ Currently: Example Provider (Test Only!)
     │
     ├─ Commissioning Credentials
     │  ├─ Setup PIN: 20202021 (Temporary)
     │  ├─ Discriminator: 3840
     │  └─ Vendor ID: 0xFFF1 (Test)
     │
     └─ NVS Storage (Flash)
        └─ Pairing Info, Fabric Data

⚠️  For Production:
    - Use real DAC certificates
    - Random setup codes
    - Registered Vendor ID

═══════════════════════════════════════════════════════════════════

                      📊 Memory Layout

Flash (4MB):
    ┌──────────────────────────┐ 0x0
    │  Bootloader              │ 32 KB
    ├──────────────────────────┤ 0x8000
    │  Partition Table         │ 4 KB
    ├──────────────────────────┤ 0x9000
    │  NVS (Non-Volatile)      │ 24 KB
    ├──────────────────────────┤ 0xF000
    │  PHY Init                │ 4 KB
    ├──────────────────────────┤ 0x10000
    │  Factory App             │ 1 MB
    │  (SmartRoom.bin)         │
    └──────────────────────────┘

RAM:
    ├─ Matter Stack:    ~100 KB
    ├─ WiFi/BLE:        ~50 KB
    ├─ FreeRTOS:        ~20 KB
    ├─ Application:     ~10 KB
    └─ Available:       ~140 KB

═══════════════════════════════════════════════════════════════════

                    🌐 Network Topology

                    [Internet]
                        │
                        │
                    [Router]
                   /    │    \
                  /     │     \
                 /      │      \
            [iPhone] [ESP32] [Other]
            (HomeKit) (Matter) (Devices)
                 \      │      /
                  \     │     /
                   \    │    /
                [Local Matter Network]
                     │
                     ├─ mDNS Discovery
                     ├─ Matter Fabric
                     └─ UDP/TCP Comm.

mDNS Service Advertisement:
    _matter._tcp.local
    _matterc._udp.local

═══════════════════════════════════════════════════════════════════

                    ⏱️ Timing Diagram

Commissioning (First Time):

    0s    ┌────────────────────────────────────┐
          │ ESP32 boots                        │
    2s    ├────────────────────────────────────┤
          │ WiFi connects                      │
    5s    ├────────────────────────────────────┤
          │ Matter advertising starts          │
    10s   ├────────────────────────────────────┤
          │ User scans QR / enters code        │
    30s   ├────────────────────────────────────┤
          │ Pairing & credential exchange      │
    60s   ├────────────────────────────────────┤
          │ Commissioning complete             │
          └────────────────────────────────────┘

Normal Operation:

    0ms   ┌────────────────────────────────────┐
          │ User taps button in Home App       │
    50ms  ├────────────────────────────────────┤
          │ Matter message received            │
    60ms  ├────────────────────────────────────┤
          │ Callback triggered                 │
    61ms  ├────────────────────────────────────┤
          │ IR signal starts transmitting      │
    129ms ├────────────────────────────────────┤
          │ IR signal complete (68ms duration) │
    130ms ├────────────────────────────────────┤
          │ Button auto-resets in Home App     │
          └────────────────────────────────────┘

═══════════════════════════════════════════════════════════════════

                    💡 LED Status Indicators
                    
(If you add a status LED in the future)

    ┌─────────────────┬──────────────────────┐
    │  Pattern        │  Meaning             │
    ├─────────────────┼──────────────────────┤
    │  Slow Blink     │  Commissioning Mode  │
    │  Fast Blink     │  WiFi Connecting     │
    │  Solid          │  Connected & Ready   │
    │  Double Blink   │  IR Signal Sent      │
    │  Triple Blink   │  Error               │
    └─────────────────┴──────────────────────┘

═══════════════════════════════════════════════════════════════════

Created: 2025-10-06
Version: 1.0
Author: vnc3nt

