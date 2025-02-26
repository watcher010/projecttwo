import streamlit as st
import websockets
import json
import pandas as pd
import plotly.graph_objects as go
from datetime import datetime
import asyncio
import extra_streamlit_components as stx

# Cloud WebSocket Configuration
WS_URL = "wss://your-cloud-websocket-server.com/ws"

class RealTimeMonitor:
    def __init__(self):
        self.rooms = {}
        self.connection = None
        self.browser_tab = stx.tab_bar()
        
        if 'initialized' not in st.session_state:
            self.initialize_session()
            
    def initialize_session(self):
        st.session_state.update({
            'initialized': True,
            'rooms': {
                '1': {'name': 'Living Room', 'threshold': 2500, 'data': []},
                '2': {'name': 'Bedroom', 'threshold': 2000, 'data': []},
                '3': {'name': 'Kitchen', 'threshold': 3000, 'data': []}
            }
        })
    
    async def connect(self):
        while True:
            try:
                self.connection = await websockets.connect(WS_URL)
                await self.handle_messages()
            except Exception as e:
                st.error(f"Connection error: {str(e)}")
                await asyncio.sleep(5)
                
    async def handle_messages(self):
        async for message in self.connection:
            data = json.loads(message)
            room_id = data['id']
            
            if room_id not in st.session_state.rooms:
                st.session_state.rooms[room_id] = {
                    'name': f"Room {room_id}",
                    'threshold': data['threshold'],
                    'data': []
                }
                
            room = st.session_state.rooms[room_id]
            room['data'].append({
                'timestamp': datetime.now(),
                'power': data['power']
            })
            
            # Keep last 60 seconds of data
            cutoff = datetime.now() - pd.Timedelta(seconds=60)
            room['data'] = [d for d in room['data'] 
                          if d['timestamp'] > cutoff]
            
            room['cutoff'] = data['cutoff']
            room['bypass'] = data['bypass']
            
    def power_graph(self, room_id):
        room = st.session_state.rooms[room_id]
        df = pd.DataFrame(room['data'])
        
        fig = go.Figure()
        fig.add_trace(go.Scatter(
            x=df['timestamp'],
            y=df['power'],
            line=dict(color='#1f77b4', width=2)
        ))
        fig.add_hline(
            y=room['threshold'],
            line_dash="dash",
            line_color="red"
        )
        fig.update_layout(
            height=300,
            margin=dict(t=30, b=20, l=40, r=20),
            showlegend=False
        )
        return fig
        
    async def send_command(self, command, room_id, value=None):
        if self.connection:
            msg = {
                'command': command,
                'id': room_id
            }
            if value is not None:
                msg['value'] = value
            await self.connection.send(json.dumps(msg))
            
    def dashboard(self):
        st.title("Cloud Power Monitor")
        
        with st.sidebar:
            st.header("Room Controls")
            with st.expander("➕ Add Custom Room"):
                with st.form("add_room"):
                    name = st.text_input("Room Name")
                    threshold = st.number_input("Threshold (W)", 100, 10000, 2500)
                    if st.form_submit_button("Create"):
                        new_id = str(len(st.session_state.rooms) + 1)
                        st.session_state.rooms[new_id] = {
                            'name': name,
                            'threshold': threshold,
                            'data': []
                        }
                        
        cols = st.columns(3)
        for idx, (room_id, room) in enumerate(st.session_state.rooms.items()):
            with cols[idx % 3]:
                card = st.container()
                with card:
                    st.subheader(room['name'])
                    
                    # Threshold Control
                    new_thresh = st.number_input(
                        "Threshold (W)",
                        key=f"thresh_{room_id}",
                        value=room['threshold'],
                        min_value=100,
                        max_value=10000,
                        on_change=lambda: asyncio.run(
                            self.send_command("updateThreshold", room_id, 
                            st.session_state[f"thresh_{room_id}"])
                        )
                    )
                    
                    # Status Indicators
                    if room.get('cutoff'):
                        st.error("Power Cutoff Active!")
                    if room.get('bypass'):
                        st.warning("Bypass Detected!")
                    
                    # Live Graph
                    st.plotly_chart(
                        self.power_graph(room_id), 
                        use_container_width=True
                    )
                    
                    # Action Buttons
                    c1, c2 = st.columns(2)
                    with c1:
                        if st.button("🔄 Reset", key=f"reset_{room_id}"):
                            await self.send_command("reset", room_id)
                    with c2:
                        if st.button("🗑️ Delete", key=f"del_{room_id}"):
                            del st.session_state.rooms[room_id]
                            st.rerun()

if __name__ == "__main__":
    monitor = RealTimeMonitor()
    asyncio.run(monitor.connect())
    monitor.dashboard()
    st_autorefresh(interval=1000, limit=100)