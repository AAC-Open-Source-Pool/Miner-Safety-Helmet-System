# MAIN CODE
from flask import Flask, request, jsonify, send_from_directory
import os
from datetime import datetime
from PIL import Image, ImageDraw, ImageFont
import io

app = Flask(__name__)

# Create uploads directory
UPLOAD_FOLDER = 'uploads'
OVERLAY_FOLDER = 'uploads_overlay'
for folder in [UPLOAD_FOLDER, OVERLAY_FOLDER]:
    if not os.path.exists(folder):
        os.makedirs(folder)

def parse_sensor_data(sensor_string):
    """Parse sensor data string from WROVER"""
    data = {
        'alert_type': 'NONE',
        'temperature': 'N/A',
        'humidity': 'N/A',
        'gas': 'N/A',
        'heart_rate': 'N/A',
        'accel': 'N/A'
    }
    
    try:
        parts = sensor_string.split('|')
        for part in parts:
            if ':' in part:
                key, value = part.split(':', 1)
                if 'FALL' in part or 'FALL' in key:
                    data['alert_type'] = 'FALL_DETECTED'
                elif 'GAS' in part and 'ALERT' in part:
                    data['alert_type'] = 'GAS_ALERT'
                elif 'TEMP' in part and 'ALERT' in part:
                    data['alert_type'] = 'TEMP_ALERT'
                elif 'HEART' in part and 'RATE' in part:
                    data['alert_type'] = 'HEART_RATE_ALERT'
                elif 'MANUAL' in key or 'STARTUP' in key or 'PERIODIC' in key:
                    data['alert_type'] = 'NONE'
                elif key == 'TEMP':
                    data['temperature'] = value if value != '0' and value != '0.0' else 'N/A'
                elif key == 'HUM':
                    data['humidity'] = value if value != '0' and value != '0.0' else 'N/A'
                elif key == 'GAS':
                    data['gas'] = value
                elif key == 'HR':
                    data['heart_rate'] = value if value != '0' else 'N/A'
                elif key == 'ACCEL':
                    data['accel'] = value
    except Exception as e:
        print(f"Error parsing sensor data: {e}")
    
    return data

def create_overlay_image(image_data, sensor_data, timestamp):
    """Add HUD overlay to image with sensor data and timestamp"""
    try:
        # Open image from bytes
        img = Image.open(io.BytesIO(image_data))
        draw = ImageDraw.Draw(img)
        
        # Try to load a font, fallback to default
        try:
            font_large = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 24)
            font_medium = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 18)
            font_small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 14)
        except:
            font_large = ImageFont.load_default()
            font_medium = ImageFont.load_default()
            font_small = ImageFont.load_default()
        
        width, height = img.size
        
        # === TOP LEFT: Timestamp ===
        timestamp_text = timestamp.strftime('%Y-%m-%d %H:%M:%S')
        
        # Background for timestamp
        bbox = draw.textbbox((0, 0), timestamp_text, font=font_medium)
        text_width = bbox[2] - bbox[0]
        text_height = bbox[3] - bbox[1]
        
        padding = 10
        draw.rectangle(
            [5, 5, 15 + text_width + padding, 15 + text_height + padding],
            fill=(0, 0, 0, 180)
        )
        draw.text((10, 10), timestamp_text, fill=(255, 255, 255), font=font_medium)
        
        # === TOP RIGHT: Sensor Data HUD ===
        hud_x = width - 250
        hud_y = 10
        line_height = 25
        
        # Background box for HUD
        hud_height = 160
        if sensor_data['alert_type'] != 'NONE':
            hud_height += 40
        
        draw.rectangle(
            [hud_x - 10, hud_y - 5, width - 5, hud_y + hud_height],
            fill=(0, 0, 0, 200),
            outline=(100, 100, 100),
            width=2
        )
        
        # Alert Type (if any)
        y_offset = hud_y
        if sensor_data['alert_type'] != 'NONE':
            alert_color = (255, 50, 50)  # Red for alerts
            if 'FALL' in sensor_data['alert_type']:
                alert_text = "⚠ FALL DETECTED"
            elif 'GAS' in sensor_data['alert_type']:
                alert_text = "⚠ GAS ALERT"
            elif 'TEMP' in sensor_data['alert_type']:
                alert_text = "⚠ TEMP ALERT"
            elif 'HEART' in sensor_data['alert_type']:
                alert_text = "⚠ HR ALERT"
            else:
                alert_text = "⚠ " + sensor_data['alert_type']
            
            draw.text((hud_x, y_offset), alert_text, fill=alert_color, font=font_large)
            y_offset += line_height + 10
        
        # Sensor readings with icons
        sensors = [
            (f"🌡 Temp: {sensor_data['temperature']}°C", (255, 255, 255)),
            (f"💧 Humidity: {sensor_data['humidity']}%", (100, 200, 255)),
            (f"☁ Gas: {sensor_data['gas']}", (255, 200, 100)),
            (f"❤ HR: {sensor_data['heart_rate']} BPM", (255, 100, 100)),
            (f"📊 Accel: {sensor_data['accel']}G", (150, 255, 150))
        ]
        
        for text, color in sensors:
            draw.text((hud_x, y_offset), text, fill=color, font=font_small)
            y_offset += line_height
        
        # Save overlayed image
        output = io.BytesIO()
        img.save(output, format='JPEG', quality=95)
        output.seek(0)
        
        return output.read()
        
    except Exception as e:
        print(f"Error creating overlay: {e}")
        return image_data  # Return original if overlay fails

@app.route('/upload', methods=['POST'])
def upload_image():
    print("\n" + "=" * 60)
    print("INCOMING REQUEST")
    print("=" * 60)
    print(f"Method: {request.method}")
    print(f"Content-Type: {request.headers.get('Content-Type', 'Not set')}")
    print(f"Content-Length: {request.headers.get('Content-Length', 'Not set')}")
    print(f"Remote Address: {request.remote_addr}")
    
    try:
        # Get image data
        image_data = request.data
        
        print(f"Received data size: {len(image_data)} bytes")
        
        if not image_data:
            print("ERROR: No image data received!")
            return jsonify({'error': 'No image data received'}), 400
        
        # Get sensor data from header
        sensor_string = request.headers.get('X-Sensor-Data', 'SENSOR_DATA:NONE|TEMP:0|HUM:0|GAS:0|HR:0|ACCEL:0')
        print(f"Raw sensor data header: {sensor_string}")
        
        sensor_data = parse_sensor_data(sensor_string)
        print(f"Parsed sensor data: {sensor_data}")
        
        # Generate timestamp
        timestamp = datetime.now()
        timestamp_str = timestamp.strftime('%Y%m%d_%H%M%S')
        
        # Create overlay image
        overlayed_image = create_overlay_image(image_data, sensor_data, timestamp)
        
        # Save original image
        original_filename = f'original_{timestamp_str}.jpg'
        original_filepath = os.path.join(UPLOAD_FOLDER, original_filename)
        with open(original_filepath, 'wb') as f:
            f.write(image_data)
        
        # Save overlayed image
        overlay_filename = f'image_{timestamp_str}.jpg'
        overlay_filepath = os.path.join(OVERLAY_FOLDER, overlay_filename)
        with open(overlay_filepath, 'wb') as f:
            f.write(overlayed_image)
        
        # Log the upload
        alert_marker = ""
        if sensor_data['alert_type'] != 'NONE':
            alert_marker = f" [⚠ {sensor_data['alert_type']}]"
        
        print(f'✓ Image saved: {overlay_filename} ({len(image_data)} bytes){alert_marker}')
        print(f'  ├─ Temperature: {sensor_data["temperature"]}°C')
        print(f'  ├─ Humidity: {sensor_data["humidity"]}%')
        print(f'  ├─ Gas Level: {sensor_data["gas"]}')
        print(f'  ├─ Heart Rate: {sensor_data["heart_rate"]} BPM')
        print(f'  └─ Acceleration: {sensor_data["accel"]}G')
        print("=" * 60)
        
        return jsonify({
            'success': True,
            'filename': overlay_filename,
            'original_filename': original_filename,
            'size': len(image_data),
            'sensor_data': sensor_data,
            'alert': sensor_data['alert_type'],
            'message': 'Image uploaded and processed successfully'
        }), 200
        
    except Exception as e:
        print(f'✗ Error: {str(e)}')
        return jsonify({'error': str(e)}), 500

@app.route('/images/<filename>')
def get_image(filename):
    """Serve uploaded images"""
    if os.path.exists(os.path.join(OVERLAY_FOLDER, filename)):
        return send_from_directory(OVERLAY_FOLDER, filename)
    return send_from_directory(UPLOAD_FOLDER, filename)

@app.route('/images')
def list_images():
    """List all uploaded images"""
    try:
        files = os.listdir(OVERLAY_FOLDER)
        images = [f for f in files if f.endswith('.jpg')]
        images.sort(reverse=True)
        
        return jsonify({
            'images': images,
            'count': len(images)
        })
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/latest')
def get_latest():
    """Get the latest image info"""
    try:
        files = os.listdir(OVERLAY_FOLDER)
        images = [f for f in files if f.endswith('.jpg')]
        if images:
            images.sort(reverse=True)
            latest = images[0]
            filepath = os.path.join(OVERLAY_FOLDER, latest)
            file_time = datetime.fromtimestamp(os.path.getmtime(filepath))
            return jsonify({
                'filename': latest,
                'timestamp': file_time.strftime('%Y-%m-%d %H:%M:%S'),
                'url': f'/images/{latest}'
            })
        return jsonify({'error': 'No images available'}), 404
    except Exception as e:
        return jsonify({'error': str(e)}), 500

@app.route('/')
def index():
    """Web interface with auto-refresh"""
    html = '''
    <!DOCTYPE html>
    <html>
    <head>
        <title>Miner Safety Helmet Monitor</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
            * {
                margin: 0;
                padding: 0;
                box-sizing: border-box;
            }
            
            body {
                font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
                background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%);
                color: white;
                min-height: 100vh;
                padding: 20px;
            }
            
            .container {
                max-width: 1400px;
                margin: 0 auto;
            }
            
            .header {
                text-align: center;
                margin-bottom: 30px;
            }
            
            h1 {
                font-size: 2.5em;
                text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
                margin-bottom: 10px;
            }
            
            .live-indicator {
                display: inline-flex;
                align-items: center;
                gap: 8px;
                background: rgba(255,255,255,0.2);
                padding: 8px 16px;
                border-radius: 20px;
                font-size: 14px;
            }
            
            .live-dot {
                width: 10px;
                height: 10px;
                background: #4ade80;
                border-radius: 50%;
                animation: pulse 2s infinite;
            }
            
            @keyframes pulse {
                0%, 100% { opacity: 1; transform: scale(1); }
                50% { opacity: 0.5; transform: scale(1.2); }
            }
            
            .controls {
                display: flex;
                justify-content: center;
                gap: 15px;
                margin: 20px 0;
                flex-wrap: wrap;
            }
            
            button {
                background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                border: none;
                color: white;
                padding: 12px 30px;
                font-size: 16px;
                border-radius: 50px;
                cursor: pointer;
                box-shadow: 0 4px 15px rgba(0,0,0,0.2);
                transition: all 0.3s ease;
                font-weight: 600;
            }
            
            button:hover {
                transform: translateY(-2px);
                box-shadow: 0 6px 20px rgba(0,0,0,0.3);
            }
            
            .status {
                text-align: center;
                margin: 20px 0;
                font-size: 18px;
                background: rgba(255,255,255,0.1);
                padding: 15px;
                border-radius: 15px;
                backdrop-filter: blur(10px);
            }
            
            .latest-image-container {
                background: rgba(255,255,255,0.1);
                backdrop-filter: blur(10px);
                border-radius: 20px;
                padding: 20px;
                box-shadow: 0 8px 32px rgba(0,0,0,0.3);
                margin-bottom: 20px;
            }
            
            .image-wrapper {
                position: relative;
                background: white;
                border-radius: 15px;
                overflow: hidden;
                box-shadow: 0 4px 15px rgba(0,0,0,0.2);
            }
            
            .image-wrapper img {
                width: 100%;
                display: block;
                cursor: pointer;
            }
            
            .alert-badge {
                position: absolute;
                top: 20px;
                left: 20px;
                background: rgba(255, 50, 50, 0.9);
                color: white;
                padding: 10px 20px;
                border-radius: 10px;
                font-weight: bold;
                font-size: 18px;
                animation: alertPulse 1s infinite;
            }
            
            @keyframes alertPulse {
                0%, 100% { opacity: 1; }
                50% { opacity: 0.7; }
            }
            
            .refresh-info {
                text-align: center;
                margin: 10px 0;
                font-size: 14px;
                opacity: 0.8;
            }
        </style>
    </head>
    <body>
        <div class="container">
            <div class="header">
                <h1>⛑️ Miner Safety Helmet Monitor</h1>
                <div class="live-indicator">
                    <div class="live-dot"></div>
                    <span>Live Monitoring Active</span>
                </div>
            </div>
            
            <div class="controls">
                <button onclick="loadLatest()">🔄 Refresh Now</button>
                <button onclick="toggleAutoRefresh()">
                    <span id="autoRefreshBtn">⏸️ Pause Auto-Refresh</span>
                </button>
            </div>
            
            <div class="refresh-info">
                Auto-refreshing every <strong>5</strong> seconds | 
                Next refresh in: <strong id="countdown">5</strong>s
            </div>
            
            <div class="status" id="status">Loading latest image...</div>
            
            <div class="latest-image-container" id="latestContainer" style="display:none;">
                <h2>📸 Latest Capture</h2>
                <div class="image-wrapper">
                    <img id="latestImage" src="" alt="Latest capture">
                </div>
            </div>
        </div>

        <script>
            let autoRefreshEnabled = true;
            let countdown = 5;
            let countdownInterval;
            
            function updateCountdown() {
                if (autoRefreshEnabled) {
                    countdown--;
                    document.getElementById('countdown').textContent = countdown;
                    
                    if (countdown <= 0) {
                        countdown = 5;
                        loadLatest();
                    }
                }
            }
            
            function startCountdown() {
                clearInterval(countdownInterval);
                countdown = 5;
                countdownInterval = setInterval(updateCountdown, 1000);
            }
            
            function loadLatest() {
                fetch('/latest')
                    .then(response => response.json())
                    .then(data => {
                        if (data.filename) {
                            const img = document.getElementById('latestImage');
                            const container = document.getElementById('latestContainer');
                            
                            img.src = data.url + '?' + new Date().getTime();
                            img.onclick = () => window.open(img.src, '_blank');
                            
                            container.style.display = 'block';
                            document.getElementById('status').textContent = 
                                '✓ Latest image loaded - ' + data.timestamp;
                            document.getElementById('status').style.background = 
                                'rgba(74, 222, 128, 0.3)';
                        } else {
                            document.getElementById('status').textContent = 
                                'No images yet. Waiting for helmet data...';
                        }
                        
                        if (autoRefreshEnabled) {
                            countdown = 5;
                        }
                    })
                    .catch(error => {
                        document.getElementById('status').textContent = 
                            'Error loading image. Retrying...';
                        console.error('Error:', error);
                    });
            }
            
            function toggleAutoRefresh() {
                autoRefreshEnabled = !autoRefreshEnabled;
                const btn = document.getElementById('autoRefreshBtn');
                
                if (autoRefreshEnabled) {
                    btn.textContent = '⏸️ Pause Auto-Refresh';
                    startCountdown();
                } else {
                    btn.textContent = '▶️ Resume Auto-Refresh';
                    clearInterval(countdownInterval);
                    document.getElementById('countdown').textContent = '--';
                }
            }
            
            loadLatest();
            startCountdown();
        </script>
    </body>
    </html>
    '''
    return html

if __name__ == '__main__':
    print('=' * 60)
    print('⛑️  MINER SAFETY HELMET MONITORING SERVER')
    print('=' * 60)
    print('✓ Image overlay with sensor data enabled')
    print('✓ Alert detection active')
    print('✓ Real-time monitoring ready')
    print('=' * 60)
    print('Server: http://192.168.241.160:5000')
    print('=' * 60)

    app.run(host='0.0.0.0', port=5000, debug=True)
