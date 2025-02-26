import streamlit as st
import asyncio
import websockets
import json
import pandas as pd
import plotly.graph_objects as go
from datetime import datetime
import time
import threading

# Initialize session state
if 'rooms' not in st.session_state:
    st.session_state.rooms = {
        '1': {'name': 'Living Room', 'threshold': 2500.0, 'data': [], 'cutoff': False, 'bypass': False, 'meas_pin': 25, 'cutoff_pin': 26},
        '2': {'name': 'Bedroom', 'threshold': 2000.0, 'data': [], 'cutoff': False, 'bypass': False, 'meas_pin': 27, 'cutoff_pin': 28},
        '3': {'name': 'Kitchen', 'threshold': 3000.0, 'data': [], 'cutoff': False, 'bypass': False, 'meas_pin': 29, 'cutoff_pin': 30}
    }

# WebSocket configuration - ESP32's AP IP address
WS_HOST = "ws://192.168.4.1:8765"

async def connect_websocket():
    """Establish WebSocket connection and handle messages"""
    while True:
        try:
            async with websockets.connect(WS_HOST) as websocket:
                st.session_state.ws_connected = True
                
                while True:
                    try:
                        message = await websocket.recv()
                        data = json.loads(message)
                        
                        # Handle different message types
                        if isinstance(data, list):  # Room data update
                            for room_data in data:
                                room_id = room_data['id']
                                if room_id in st.session_state.rooms:
                                    room = st.session_state.rooms[room_id]
                                    timestamp = datetime.now()
                                    
                                    # Update room data
                                    room['data'].append({
                                        'timestamp': timestamp,
                                        'power': room_data['display_power']
                                    })
                                    
                                    # Keep only last 60 seconds of data
                                    current_time = time.time()
                                    room['data'] = [d for d in room['data'] 
                                                  if (current_time - d['timestamp'].timestamp()) <= 60]
                                    
                                    room['cutoff'] = room_data['isCutoff']
                                    room['bypass'] = room_data['bypassDetected']
                        
                        elif isinstance(data, dict):  # Command response
                            if 'status' in data:
                                if data['status'] == 'error':
                                    st.error(data['message'])
                                elif data['status'] == 'success':
                                    st.success(data['message'])
                                    
                    except websockets.ConnectionClosed:
                        st.session_state.ws_connected = False
                        break
                        
        except Exception as e:
            st.session_state.ws_connected = False
            st.error(f"WebSocket Error: {str(e)}")
            await asyncio.sleep(5)  # Wait before retrying

def start_websocket():
    """Start WebSocket connection in a separate thread"""
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    loop.run_until_complete(connect_websocket())

# Start WebSocket connection in background
threading.Thread(target=start_websocket, daemon=True).start()

def create_power_graph(room_id):
    """Create real-time power consumption graph using Plotly"""
    room = st.session_state.rooms[room_id]
    df = pd.DataFrame(room['data'])
    
    if len(df) > 0:
        fig = go.Figure()
        
        # Add power line
        fig.add_trace(go.Scatter(
            x=df['timestamp'],
            y=df['power'],
            name='Power',
            line=dict(color='blue', width=2)
        ))
        
        # Add threshold line
        fig.add_hline(
            y=room['threshold'],
            line_dash="dash",
            line_color="red",
            annotation_text="Threshold"
        )
        
        fig.update_layout(
            title=f"{room['name']} Power Consumption",
            xaxis_title="Time",
            yaxis_title="Power (W)",
            height=400,
            margin=dict(l=20, r=20, t=40, b=20)
        )
        
        return fig
    return None

async def send_command(room_id, action, **kwargs):
    """Send command to ESP32"""
    try:
        async with websockets.connect(WS_HOST) as websocket:
            command = {
                "action": action,
                "room_id": room_id,
                **kwargs
            }
            await websocket.send(json.dumps(command))
    except Exception as e:
        st.error(f"Failed to send command: {str(e)}")

async def send_threshold_update(room_id, threshold):
    """Send threshold update to ESP32"""
    await send_command(room_id, "update", threshold=threshold)

def main():
    st.title("Power Monitoring Dashboard")
    
    # Connection status
    status = st.sidebar.empty()
    if st.session_state.get('ws_connected', False):
        status.success("Connected to ESP32")
    else:
        status.error("Disconnected from ESP32")
    
    # Room management
    st.sidebar.header("Room Management")
    
    # Add new room
    with st.sidebar.expander("Add New Room"):
        with st.form("add_room"):
            new_room_name = st.text_input("Room Name")
            new_room_threshold = st.number_input("Power Threshold (W)", 
                                               min_value=100.0, 
                                               max_value=10000.0, 
                                               value=2500.0)
            new_meas_pin = st.number_input("Measurement Relay GPIO Pin", 
                                         min_value=0, 
                                         max_value=39, 
                                         value=25)
            new_cutoff_pin = st.number_input("Cutoff Relay GPIO Pin", 
                                           min_value=0, 
                                           max_value=39, 
                                           value=26)
            if st.form_submit_button("Add Room"):
                new_id = str(len(st.session_state.rooms) + 1)
                st.session_state.rooms[new_id] = {
                    'name': new_room_name,
                    'threshold': new_room_threshold,
                    'data': [],
                    'cutoff': False,
                    'bypass': False,
                    'meas_pin': new_meas_pin,
                    'cutoff_pin': new_cutoff_pin
                }
                asyncio.run(send_command(new_id, "add", 
                                       name=new_room_name,
                                       threshold=new_room_threshold,
                                       meas_pin=new_meas_pin,
                                       cutoff_pin=new_cutoff_pin))
    
    # Display rooms
    cols = st.columns(3)
    for idx, (room_id, room) in enumerate(st.session_state.rooms.items()):
        col = cols[idx % 3]
        with col:
            st.subheader(room['name'])
            
            # Threshold adjustment
            new_threshold = st.number_input(
                "Threshold (W)",
                min_value=100.0,
                max_value=10000.0,
                value=float(room['threshold']),
                key=f"threshold_{room_id}"
            )
            if new_threshold != room['threshold']:
                room['threshold'] = new_threshold
                asyncio.run(send_threshold_update(room_id, new_threshold))
            
            # Status indicators
            if room['cutoff']:
                st.error("⚠️ Power Cut Off - Threshold Exceeded!")
            if room['bypass']:
                st.warning("⚡ Potential Bypass Detected!")
            
            # Power graph
            fig = create_power_graph(room_id)
            if fig:
                st.plotly_chart(fig, use_container_width=True)
            
            # Room controls
            col1, col2 = st.columns(2)
            with col1:
                if st.button("Reset Power", key=f"reset_{room_id}"):
                    asyncio.run(send_command(room_id, "reconnect"))
            with col2:
                if st.button("Delete Room", key=f"delete_{room_id}"):
                    asyncio.run(send_command(room_id, "remove"))
                    del st.session_state.rooms[room_id]
                    st.experimental_rerun()

if __name__ == "__main__":
    main()