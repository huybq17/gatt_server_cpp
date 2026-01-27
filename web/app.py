from flask import Flask, render_template
from flask_socketio import SocketIO
import threading
import time
import os

app = Flask(__name__)
# Enable CORS just in case, allow all origins for development
socketio = SocketIO(app, cors_allowed_origins="*")

THERMAL_ZONE_PATH = "/sys/class/thermal/thermal_zone0/temp"

def read_cpu_temp():
    try:
        if os.path.exists(THERMAL_ZONE_PATH):
            with open(THERMAL_ZONE_PATH, "r") as f:
                temp_str = f.read().strip()
                # Convert milli-degrees to degrees Celsius
                return int(temp_str) / 1000.0
    except Exception as e:
        print(f"Error reading temp: {e}")
    return 0.0

def background_thread():
    """Continuously read temperature and push to clients."""
    print("Starting background temperature monitoring...")
    last_temp = -1000.0
    while True:
        try:
            current_temp = read_cpu_temp()
            # print(f"Read temp: {current_temp}") # Debug print
            
            # Broadcast regardless of change for debug purposes initially
            socketio.emit('temp_update', {'temperature': current_temp})
            socketio.sleep(2) # Important: Use socketio.sleep instead of time.sleep
        except Exception as e:
            print(f"Thread error: {e}")
            socketio.sleep(2)

@app.route('/')
def index():
    return render_template('index.html')

@socketio.on('connect')
def test_connect():
    print('Client connected')
    # Start background thread only once when first client connects, or use simple check
    global thread
    with thread_lock:
        if thread is None:
            thread = socketio.start_background_task(background_thread)

thread = None
thread_lock = threading.Lock()

if __name__ == '__main__':
    # Run server on port 5000
    print("Starting Web Server on http://127.0.0.1:5000/")
    socketio.run(app, host='0.0.0.0', port=5000)
