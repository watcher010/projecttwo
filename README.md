# Power Monitoring Dashboard

This Streamlit dashboard connects to an ESP32 running a PZEM-004T power monitor and displays real-time power consumption data for multiple rooms.

## Features

- Real-time power monitoring
- Multiple room support
- Configurable power thresholds
- Automatic power cutoff on threshold exceed
- Bypass detection
- Interactive graphs
- Room management (add/remove rooms)

## Setup

1. Install requirements:
   ```bash
   pip install -r requirements.txt
   ```

2. Update WebSocket configuration:
   - Open `app.py`
   - Update `WS_HOST` with your ESP32's IP address and port

3. Run the dashboard:
   ```bash
   streamlit run app.py
   ```

## Usage

- Add rooms using the sidebar
- Monitor power consumption in real-time
- Adjust power thresholds as needed
- Reset power or delete rooms using the control buttons
- Watch for threshold warnings and bypass detection