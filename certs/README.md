# AWS IoT Certificates

This directory holds the device certificates for `ESP32_HospitalBed_01`.

**These files are excluded from version control for security reasons.**

## Required files (obtain from AWS IoT Core → Things → Certificates):

| File | Purpose |
|------|---------|
| `ESP32_HospitalBed_01.cert.pem` | Device certificate |
| `ESP32_HospitalBed_01.private.key` | Device private key (**keep secret**) |
| `ESP32_HospitalBed_01.public.key` | Device public key |
| `root-CA.crt` | Amazon Root CA1 |

## Evaluator Access (read-only IoT console)

- **Console:** https://112188881314.signin.aws.amazon.com/console  
- **IAM Username:** `evaluator_parth_intern`  
- **Password:** `HospitalBed@2026!`  
- **Region:** ap-southeast-2  
- **Thing Name:** `ESP32_HospitalBed_01`  
- **Subscribe to:** `hospital_parth_bed01_2026/ward_A/bed_01/#`
