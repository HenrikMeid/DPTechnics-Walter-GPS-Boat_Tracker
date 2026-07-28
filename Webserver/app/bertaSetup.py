import math
import os # json speichern
import json
from werkzeug.security import generate_password_hash, check_password_hash

def load_from_settings(PATH_TO_FILE):
    if os.path.exists(PATH_TO_FILE):
        try:
            with open(PATH_TO_FILE, "r", encoding="utf-8") as f:
                data = json.load(f)
                if isinstance(data, dict):
                    return data
        except (json.JSONDecodeError, OSError):
            pass
    return {}

def save_to_settings(PATH_TO_FILE, newData):
    data = load_from_settings(PATH_TO_FILE)
    data.update(newData)

    with open(PATH_TO_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=4)

# Benutzerdatenbank (Passwörter gehasht)
def setup_users(PATH):
    global users
    settings = load_from_settings(PATH)
    
    # print("Settings path:", PATH)
    # print("Settings content:", settings)

    users = {}
    for username, password in settings.get("users", {}).items():
        users[username] = generate_password_hash(password)

    print("INFO: Users loaded")
    # print(users.keys())
    return users

def haversine_meter_distance(lat1, lon1, lat2, lon2):
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

def calculate_meter_track_length(track):
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

        distance = haversine_meter_distance(
            p1["lat"], p1["lon"],
            p2["lat"], p2["lon"]
        )

        if 150 < distance <= 50000: # Ausreisser (GPS-Sprung > 50 km) ignorieren
            totalDistance += distance
        else :
            totalDistance += 0 # eyplizit muss

    return totalDistance

def remove_position_outliers(points, max_jump_meter=50000):
    """
    Entfernt einzelne Positions-Ausreisser (z.B. fehlerhafte GPS-Fixes) aus
    einer chronologisch sortierten Punktliste. Ein Punkt gilt als Ausreisser,
    wenn er sowohl vom vorherigen als auch vom naechsten Punkt mehr als
    max_jump_meter entfernt liegt. Erwartet Punkte mit 'lat' und 'lon'.
    """
    if len(points) < 3:
        return points[:]

    filtered = [points[0]]

    for i in range(1, len(points) - 1):
        prevDistance = haversine_meter_distance(
            points[i - 1]["lat"], points[i - 1]["lon"],
            points[i]["lat"], points[i]["lon"]
        )
        nextDistance = haversine_meter_distance(
            points[i]["lat"], points[i]["lon"],
            points[i + 1]["lat"], points[i + 1]["lon"]
        )

        if prevDistance > max_jump_meter and nextDistance > max_jump_meter:
            continue # Ausreisser -> ueberspringen

        filtered.append(points[i])

    filtered.append(points[-1])
    return filtered

def isInsidePort(lat, lon, port_coordinates):
    """
    Prueft ob eine Position innerhalb des Hafenrechtecks liegt.
    Spiegelt die Bounds-Pruefung von evalPosition() im ESP32 Code (berta_states.cpp).
    """
    right = (port_coordinates or {}).get("port_bound_right", {})
    left = (port_coordinates or {}).get("port_bound_left", {})

    try:
        return (lat >= left["lat"] and lat <= right["lat"] and
                lon >= right["lon"] and lon <= left["lon"])
    except (KeyError, TypeError):
        return False

def calculateTrips(entries, port_coordinates):
    """
    Erkennt Ausfahrten (Trips) aus einer chronologisch aufsteigend sortierten
    Liste von SensorData Eintraegen (Attribute .sensor, .wert, .zeitstempel).

    Eine Ausfahrt beginnt mit der letzten vom ESP gemeldeten 'position'
    innerhalb des Hafens vor dem Ablegen (falls vorhanden), gefolgt von den
    'position' Eintraegen ausserhalb des Hafens, und endet mit der ersten vom
    ESP gemeldeten 'position' innerhalb des Hafens nach der Rueckkehr (falls
    vorhanden), bevor der naechste 'state=hafen' Eintrag die Ausfahrt
    abschliesst (Muster: Hafen -> Position(en) ausserhalb -> Hafen). So
    werden Heraus- und Hineinfahren aus/in den Hafen mit dargestellt und in
    die Distanz-/Dauerberechnung einbezogen, ohne Koordinaten zu erfinden.

    Rueckgabe: Liste von dicts mit 'start'/'end' (datetime), 'durationHours'
    und 'distanceKm', chronologisch aufsteigend.
    """
    # Positions-Ausreisser (einzelne fehlerhafte GPS-Fixes) vorab erkennen,
    # damit ein einzelner falscher Wert nicht faelschlich als "ausserhalb
    # des Hafens" gewertet wird und eine Phantom-Ausfahrt erzeugt.
    rawPositions = []
    for entry in entries:
        if entry.sensor != "position":
            continue
        try:
            lat, lon = map(float, entry.wert.split(","))
        except ValueError:
            continue
        rawPositions.append({"lat": lat, "lon": lon, "timeRx": entry.zeitstempel})

    validTimes = {p["timeRx"] for p in remove_position_outliers(rawPositions)}

    trips = []
    tripPoints = []
    sawHafenAnchor = False
    lastPortPosition = None      # letzte vom ESP gemeldete Position im Hafen vor dem Ablegen
    returnPortPosition = None    # vom ESP gemeldete Position im Hafen nach der Rueckkehr
    awaitingReturnPoint = False  # 'state=hafen' kam, aber noch keine Position seitdem gemeldet

    for entry in entries:
        if entry.sensor == "state" and entry.wert == "hafen":
            if tripPoints:
                if returnPortPosition:
                    tripPoints.append(returnPortPosition)
                    trips.append(_buildTrip(tripPoints))
                    tripPoints = []
                    returnPortPosition = None
                    awaitingReturnPoint = False
                else:
                    # Rueckkehr-Position wurde noch nicht gemeldet (kommt evtl. erst
                    # nach dem state=hafen Eintrag) -> Trip erst bei der naechsten
                    # 'position' Meldung abschliessen, statt den Rueckkehrpunkt zu verlieren.
                    awaitingReturnPoint = True
            sawHafenAnchor = True

        elif entry.sensor == "position" and sawHafenAnchor:
            if entry.zeitstempel not in validTimes:
                continue # Ausreisser -> ignorieren

            try:
                lat, lon = map(float, entry.wert.split(","))
            except ValueError:
                continue

            point = {"lat": lat, "lon": lon, "timeRx": entry.zeitstempel}
            inside = isInsidePort(lat, lon, port_coordinates)

            if awaitingReturnPoint and tripPoints:
                tripPoints.append(point)
                trips.append(_buildTrip(tripPoints))
                tripPoints = []
                awaitingReturnPoint = False
                lastPortPosition = point if inside else None
                continue

            if inside:
                if tripPoints:
                    returnPortPosition = point
                else:
                    lastPortPosition = point
            else:
                if not tripPoints and lastPortPosition:
                    tripPoints.append(lastPortPosition)
                tripPoints.append(point)
                returnPortPosition = None

    # Letzte, noch nicht per 'state=hafen' + Rueckkehrposition abgeschlossene
    # Ausfahrt trotzdem ausgeben (z.B. wenn die Rueckkehrposition noch aussteht).
    if tripPoints:
        if returnPortPosition:
            tripPoints.append(returnPortPosition)
        trips.append(_buildTrip(tripPoints))

    return trips

def _buildTrip(points):
    start = points[0]["timeRx"]
    end = points[-1]["timeRx"]
    distanceMeter = calculate_meter_track_length(points)

    return {
        "start": start,
        "end": end,
        "durationHours": (end - start).total_seconds() / 3600,
        "distanceKm": distanceMeter / 1000,
        "points": points
    }
