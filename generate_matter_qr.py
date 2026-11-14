#!/usr/bin/env python3
"""
Matter QR Code Generator für ESP32-C6 SmartRoom
Generiert den korrekten QR-Code String nach Matter/CSA Spezifikation
"""

# Matter-Konfiguration aus sdkconfig.defaults
VERSION = 0
VENDOR_ID = 65521      # 0xFFF1
PRODUCT_ID = 32768     # 0x8000
FLOW_TYPE = 0          # Standard commissioning
DISCRIMINATOR = 3840   # 0x0F00
PASSCODE = 20202021

# Matter Base-38 Alphabet
BASE38_ALPHABET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-."


def base38_encode(value):
    """Konvertiert Integer zu Base38 String"""
    if value == 0:
        return BASE38_ALPHABET[0]
    
    result = []
    while value > 0:
        result.append(BASE38_ALPHABET[value % 38])
        value //= 38
    
    return ''.join(reversed(result))


def generate_matter_qr_code():
    """
    Generiert Matter QR Code nach CSA Spezifikation
    
    Bit Layout (76 bits total):
    - Bits 0-2:   Version (3 bits)
    - Bits 3-18:  Vendor ID (16 bits)
    - Bits 19-34: Product ID (16 bits)
    - Bits 35-36: Flow Type (2 bits)
    - Bits 37-48: Discriminator (12 bits)
    - Bits 49-75: Passcode (27 bits)
    """
    
    # Baue Payload Bit für Bit auf
    payload = 0
    payload |= (VERSION & 0x7)                    # 3 bits
    payload |= (VENDOR_ID & 0xFFFF) << 3          # 16 bits
    payload |= (PRODUCT_ID & 0xFFFF) << 19        # 16 bits
    payload |= (FLOW_TYPE & 0x3) << 35            # 2 bits
    payload |= (DISCRIMINATOR & 0xFFF) << 37      # 12 bits
    payload |= (PASSCODE & 0x7FFFFFF) << 49       # 27 bits
    
    # Konvertiere zu Base38
    encoded = base38_encode(payload)
    
    # Füge Matter Prefix hinzu
    qr_code = f"MT:{encoded}"
    
    return qr_code


def main():
    qr_code = generate_matter_qr_code()
    
    print()
    print("=" * 60)
    print("  MATTER QR CODE FÜR ESP32-C6 SmartRoom")
    print("=" * 60)
    print()
    print(f"  QR-Code: {qr_code}")
    print()
    print("=" * 60)
    print()
    print("Für Alexa App eingeben:")
    print(f"  → {qr_code}")
    print()
    print("Alternative (einfache PIN):")
    print(f"  → {PASSCODE}")
    print()
    print("=" * 60)
    print()


if __name__ == "__main__":
    main()
