import math
import os # json speichern
import json
from werkzeug.security import generate_password_hash, check_password_hash

def loadFromSettings(PATH_TO_FILE):
    if os.path.exists(PATH_TO_FILE):
        try:
            with open(PATH_TO_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
                if isinstance(data, dict):
                    return data
        except (json.JSONDecodeError, OSError):
            pass
    return {}

def saveToSettings(PATH_TO_FILE, newData):
    data = loadFromSettings(PATH_TO_FILE)
    data.update(newData)

    with open(PATH_TO_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=4)

# Benutzerdatenbank (Passwörter gehasht)
def setupUsers(PATH):
    global users
    settings = loadFromSettings(PATH)

    users = {}
    for username, password in settings.get("users", {}).items():
        users[username] = generate_password_hash(password)

    print("INFO: Users loaded")
    return users

def harvesineMeterDistance(lat1, lon1, lat2, lon2):
    """
    Berechnet die Entfernung zwischen zwei Punkten auf der Erde
    anhand der Haversine-Formel.

    Parameter:
    lat1, lon1 -- Koordinaten Punkt 1 (Grad)
    lat2, lon2 -- Koordinaten Punkt 2 (Grad)

    Rückgabewert:
    Distanz in Metern
    """
    
    R = 6371000 # Erdradius in Metern

    lat1 = math.radians(lat1) # Grad in Radiant umrechnen
    lon1 = math.radians(lon1)
    lat2 = math.radians(lat2)
    lon2 = math.radians(lon2)

    dlat = lat2 - lat1
    dlon = lon2 - lon1

    
    a = math.sin(dlat / 2)**2 + math.cos(lat1) * math.cos(lat2) * math.sin(dlon / 2)**2 # Haversine-Formel

    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))

    distMeter = R * c

    return distMeter

def calculateMeterTrackLength(track):
    """
    Berechnet die Gesamtlänge eines Tracks in Kilometern.
    Erwartet eine Liste von Punkten mit 'lat' und 'lon'.
    """
    if len(track) < 2:
        return 0.0

    totalDistance = 0.0

    for i in range(len(track) - 1):
        p1 = track[i]
        p2 = track[i + 1]

        distance = harvesineMeterDistance(
            p1["lat"], p1["lon"],
            p2["lat"], p2["lon"]
        )

        if distance > 150:
            totalDistance += distance
        else :
            totalDistance += 0

    return totalDistance

