import time
import requests
import base64
import serial
import json
from PIL import Image
from io import BytesIO
import os
from dotenv import load_dotenv

load_dotenv("src/.env") #if not use env pls remove this
CLIENT_ID = os.getenv("CLIENT_ID") #spotify CLIENT_ID
CLIENT_SECRET = os.getenv("CLIENT_SECRET") #spotify CLIENT_SECRET
REFRESH_TOKEN = os.getenv("REFRESH_TOKEN") #spotify REFRESH_TOKEN 


SERIAL_PORT = "COM4" #esp32 comport
BAUD_RATE = 921600 

def refresh_token():
    url = "https://accounts.spotify.com/api/token"
    auth_str = f"{CLIENT_ID}:{CLIENT_SECRET}"
    b64_auth = base64.b64encode(auth_str.encode()).decode()

    payload = {
        "grant_type": "refresh_token",
        "refresh_token": REFRESH_TOKEN
    }
    headers = {
        "Authorization": f"Basic {b64_auth}",
        "Content-Type": "application/x-www-form-urlencoded"
    }
    r = requests.post(url, data=payload, headers=headers)
    r.raise_for_status()
    return r.json()["access_token"]

def get_current_playing(token):
    url = "https://api.spotify.com/v1/me/player/currently-playing"
    headers = {"Authorization": f"Bearer {token}"}
    r = requests.get(url, headers=headers)
    if r.status_code == 200:
        return r.json()
    return None

def download_album_as_b64(url):
    r = requests.get(url)
    if r.status_code == 200:
        img = Image.open(BytesIO(r.content))
        img = img.resize((200, 200))
        buf = BytesIO()
        img.save(buf, format="JPEG")
        return base64.b64encode(buf.getvalue()).decode("utf-8")
    return ""

if __name__ == "__main__":
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    token = refresh_token()
    last_refresh = time.time()
    last_track = ""

    while True:
        if time.time() - last_refresh > 3600:
            token = refresh_token()
            last_refresh = time.time()

        data = get_current_playing(token)
        if data and "item" in data:
            track = data["item"]["name"]
            artist = data["item"]["artists"][0]["name"]
            progress = data["progress_ms"]
            duration = data["item"]["duration_ms"]

            msg = {
                "track": track,
                "artist": artist,
                "progress": progress,
                "duration": duration
            }

            if track != last_track:
                album_url = data["item"]["album"]["images"][0]["url"]
                album_b64 = download_album_as_b64(album_url)
                msg["albumArt_b64"] = album_b64
                print("Album Base64 length:", len(album_b64))
                last_track = track


            js = json.dumps(msg)
            print("JSON size:", len(js))
            ser.write((js + "\n").encode())
            ser.write((js + "\n").encode())
            print("Sent:", track, "-", artist)
        while ser.in_waiting:
            line = ser.readline().decode(errors="ignore").strip()
            if line:
                print("ESP32:", line)

        time.sleep(1)