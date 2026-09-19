# Pool Plant Monitor and Preventative Maintenance Advisor

Final-year Mechatronics Engineering project (EEE4022S, UCT) — a low-cost, bench-top proof-of-concept IoT system for monitoring aquatic facility pump condition, water quality, and occupancy, presented through a locally hosted web dashboard.

**Student:** Nyeleti Mushwana (MSHNYE002)
**Supervisor:** Dr Simon Winberg

## What this project does

Three sensing modules feed a single ESP32-based dashboard, hosted locally with no cloud dependency:

- **Vibration / motor condition monitoring** — MPU-6050 accelerometer on a small motor test rig, threshold-based fault detection.
- **Water quality** — pH (DFRobot SEN0161), temperature (DS18B20), and turbidity (DFRobot analog turbidity sensor), calibrated against known reference standards.
- **Occupancy** — camera-based motion detection (frame-differencing only, no face detection, no image storage), for non-identifying presence/access monitoring.

## Tech stack

- **Hardware:** ESP32 (38-pin dev board), ESP32-S3 with camera
- **Firmware:** C++ (Arduino framework) via PlatformIO
- **IDE:** VS Code + PlatformIO
- **Simulation/testing:** Wokwi (ESP32 simulator), Python (early algorithm validation)

## Project status

🚧 In progress — early development.

## Repository structure — what each folder is actually for

```
├── src/            # Your main firmware code lives here (main.cpp).
│                    # This is the file that actually runs on the ESP32.
├── include/         # Header files (.h) if you split code into multiple
│                    # files later — declarations go here, not logic.
├── lib/             # For any custom, project-specific library code you
│                    # write yourself (not needed yet — third-party
│                    # libraries like Adafruit MPU6050 are managed
│                    # automatically by PlatformIO instead, listed in
│                    # platformio.ini, not stored in this folder).
├── docs/            # Notes, datasheets, calibration records — anything
│                    # written/reference material relevant to the build.
└── platformio.ini   # The project's configuration file: which board,
                      # which framework, and which libraries to install.
```

## Building this project

1. Install [VS Code](https://code.visualstudio.com/) and the [PlatformIO extension](https://platformio.org/install/ide?install=vscode).
2. Clone this repository and open it in VS Code.
3. PlatformIO will automatically install the dependencies listed in `platformio.ini`.
4. Connect the relevant ESP32 board and select the correct port.
5. Build and upload via the PlatformIO toolbar.
