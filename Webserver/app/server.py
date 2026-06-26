from flask import Flask, request, Response, render_template, redirect, url_for, jsonify
from flask_sqlalchemy import SQLAlchemy # Fuer Datenbank
from flask_httpauth import HTTPBasicAuth  # Fehlender Import!
from werkzeug.security import generate_password_hash, check_password_hash
import csv # export csv
import io # export csv
import json
from datetime import datetime, timezone # Fuer isotime
from sqlalchemy import func # jsonify
import math
import os # json speichern
import logging
import bertaSetup as bs

ESP_SETTINGS_PATH = "esp_settings.json"
SETTINGS_PATH = "settings.json"
users = {}

# Flask-App und Auth initialisieren
app = Flask(__name__)
auth = HTTPBasicAuth()  # Nach app = Flask(__name__) verschoben!
app.config['MAX_CONTENT_LENGTH'] = 1 * 1024 * 1024  # 1 MB Limit
app.config['SECRET_KEY'] = os.environ.get('SECRET_KEY', 'fallback-dev-key') # Secret key from env. variable
# import secrets
# secrets.token_hex(32) # Fuer Production or in bash: openssl rand -hex 32
# export SECRET_KEY="hier_dein_generierter_key" # (bash)

# Logging für Docker+Gunicorn
handler = logging.StreamHandler() # std out log
handler.setLevel(logging.INFO)
formatter = logging.Formatter("%(asctime)s [%(levelname)s] %(message)s")
handler.setFormatter(formatter)

handler2 = logging.FileHandler("/app/logs/app.log")  # Datei log
logging.basicConfig(
    level=logging.INFO,
    handlers=[handler, handler2],
    format="%(asctime)s [%(levelname)s] %(message)s"
)

app.logger.handlers = [] # flask -> gunicorn -> docker
app.logger.addHandler(handler)
app.logger.setLevel(logging.INFO)
app.logger.propagate = False

users = bs.setupUsers(SETTINGS_PATH)

# Passwort-Prüfungsfunktion
@auth.verify_password
def verify_password(username, password):
    if username in users and check_password_hash(users.get(username), password):
        return username

# Datenbank-Konfiguration
BASE_DIR = os.path.abspath(os.path.dirname(__file__)) # Uses absoulute filepath
db_path = os.path.join(BASE_DIR, "data", "sensor_data.db")
app.config['SQLALCHEMY_DATABASE_URI'] = f"sqlite:///{db_path}"

db = SQLAlchemy(app)

# Datenbank-Modell
class SensorData(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    sensor = db.Column(db.String(50))
    wert = db.Column(db.String(100))  # <-- als String speichern
    zeitstempel = db.Column(db.DateTime, default=db.func.current_timestamp(), unique=True)
# start added 27.12.25

@app.route('/daten', methods=['POST', 'GET']) # Route zum Empfangen der Sensordaten (POST)
@auth.login_required
def daten_empfangen():
    if request.method == 'POST':
        sensor = request.form.get('sensor')
        wert = request.form.get('wert')
        if not sensor or not wert:
            return "Fehlende Felder", 400
        
        neue_daten = SensorData(sensor=sensor, wert=wert) # Wert einfach als String speichern
        db.session.add(neue_daten)
        db.session.commit()

        xff = request.headers.get("X-Forwarded-For") # Returns ip of client
        if xff:
            request_ip = xff.split(",")[0].strip()
        else:
            request_ip = request.remote_addr

        app.logger.info(f"Request from IP (app.logger) {request_ip}")

        return "OK", 200
    elif request.method == 'GET':
        port_coordinates = bs.loadFromSettings(ESP_SETTINGS_PATH).get("port_coordinates", {})

        return jsonify(port_coordinates), 200 

# Route zum Empfangen der Sensordaten (POST) GPS
@app.route('/karte', methods=["GET"])
@auth.login_required
def karte():
    latestPosTupel = SensorData.query.filter_by(sensor="position") \
        .order_by(SensorData.zeitstempel.desc()) \
        .first()

    lat = lon = timeRx = None
    if latestPosTupel:
        try:
            # String splitten (z.B. "46.775209,8.376750")
            lat, lon = latestPosTupel.wert.split(",")
            timeRx = latestPosTupel.zeitstempel.isoformat()
        except ValueError:
            pass

    return render_template("karte.html", lat=lat, lon=lon, timeRx=timeRx, live=True)


# Route zum tracken der GPS Werte
@app.route('/api/track', methods=["GET"])
@auth.login_required
def karteTrajectory():
    start_str = request.args.get("start")
    end_str   = request.args.get("end")

    if not start_str or not end_str:
        return jsonify(ok=False, reason="missing dates")

    try:
        #start = datetime.fromisoformat(start_str.replace("Z", "+00:00")).astimezone(timezone.utc)
        #end   = datetime.fromisoformat(end_str.replace("Z", "+00:00")).astimezone(timezone.utc)
        start = datetime.fromisoformat(start_str.replace("Z", "+00:00"))
        end   = datetime.fromisoformat(end_str.replace("Z", "+00:00"))

        logging.info(f"INFO: {start_str}")
        logging.info(f"INFO: {end_str}")
    except ValueError:
        return jsonify(ok=False, reason="invalid date format")

    positions = (
    SensorData.query
    .filter_by(sensor="position")
    .filter(SensorData.zeitstempel >= start)
    .filter(SensorData.zeitstempel <= end)
    .order_by(SensorData.zeitstempel.asc())
    .all()
    )

    track = []
    for p in positions:
        try:
            lat, lon = p.wert.split(",")
            track.append({
                "lat": float(lat),
                "lon": float(lon),
                "timeRx": p.zeitstempel.isoformat()
            })
        except ValueError:
            continue
    totalDistance = bs.calculateMeterTrackLength(track)
    totalKMDistance = totalDistance /1000

    return jsonify(ok=True, track=track, totalDistance=totalKMDistance)

# Ende added 27.12.25

# Route für die Webseite (GET)
@app.route('/', methods=["GET", "POST"])
@auth.login_required  # Schützt die Webseite mit Passwort
def index():
    if request.method == "GET":
        daten = SensorData.query.order_by(SensorData.zeitstempel.desc()).limit(100).all()
        return render_template('index.html', daten=daten, live=True)

    elif request.method == "POST":
        START_DATE_STR = request.form["start_date"]
        END_DATE_STR = request.form["end_date"]
        try:
            START_DATE = datetime.strptime(START_DATE_STR, "%Y-%m-%dT%H:%M")
            END_DATE = datetime.strptime(END_DATE_STR, "%Y-%m-%dT%H:%M")
        except ValueError:
            return "Ungültiges Datum", 400
        

        logging.info(f"INFO: {START_DATE}")
        logging.info(f"INFO: {END_DATE}")

        datenIntervall = (SensorData.query
                        .filter(SensorData.zeitstempel >= START_DATE)
                        .filter(SensorData.zeitstempel <= END_DATE)
                        .order_by(SensorData.zeitstempel.desc())
                        .all())

        return render_template('index.html', daten=datenIntervall, live=False)


@app.route('/dashboard', methods=["GET", "PUT"])
@auth.login_required
def dashboard():
    if request.method == "GET":
        datenIntervall = (SensorData.query
                    .filter(SensorData.sensor == "pumpe")
                    .order_by(SensorData.zeitstempel.desc())
                    .limit(6)
                    .all())

        return render_template("dashboard.html", daten=datenIntervall)

    if request.method == "PUT":
        datenIntervall = (SensorData.query
            .filter(SensorData.sensor == "pumpe")
            .order_by(SensorData.zeitstempel.desc())
            .limit(1)
            .all())
        if datenIntervall:
            pumpState = datenIntervall[0].wert == "True"  # oder True/False je nach DB
        else:
            pumpState = False

        return jsonify(ok=True, pumpState=pumpState)



@app.route('/api/daten', methods=["GET", "POST", "PUT"])
@auth.login_required
def api_daten():
    if request.method == "PUT":
        recData = request.get_json()  # PUT Body als JSON
        reqNumEntries = recData.get("reqNumEntries", 10)  # default 10
        try:
            reqNumEntries = int(reqNumEntries)
        except (ValueError, TypeError):
            reqNumEntries = None 


        daten = SensorData.query.order_by(SensorData.zeitstempel.desc()).limit(reqNumEntries).all()

 
        return jsonify({
            "daten": [
                {
                    "sensor": d.sensor,
                    "wert": d.wert,
                    "zeit": d.zeitstempel.strftime("%Y-%m-%d %H:%M:%S")
                }
                for d in daten
            ]
        })

    if request.method == "GET":
        time_rx_min = db.session.query(func.min(SensorData.zeitstempel)).scalar()
        time_rx_max = db.session.query(func.max(SensorData.zeitstempel)).scalar()
        num_entries = db.session.query(func.count(SensorData.id)).scalar()

        port_coordinates = bs.loadFromSettings(ESP_SETTINGS_PATH).get("port_coordinates") or {
            "port_bound_right": {"lat": 0, "lon": 0},
            "port_bound_left": {"lat": 0, "lon": 0}
        }

        return jsonify({
            "info": {
                "timeRxMin": time_rx_min.isoformat() + "Z" if time_rx_min else None,
                "timeRxMax": time_rx_max.isoformat() + "Z" if time_rx_max else None,
                "numOfEntrys": num_entries
            },
            "port_coordinates": port_coordinates
        })

    if request.method == "POST":
        data = request.get_json() 
        if not data or "port_bounds" not in data:
            return jsonify({"error": "port_bounds fehlt"}), 400

        pb = data["port_bounds"]
        try:
            nw = pb["north_west"]
            se = pb["south_east"]

            port_coordinates = {"port_coordinates": {
                "port_bound_right": {"lat": float(nw["lat"]), "lon": float(nw["lon"])},
                "port_bound_left":  {"lat": float(se["lat"]), "lon": float(se["lon"])}
            }
            }

            bs.saveToSettings(ESP_SETTINGS_PATH, port_coordinates)

            return jsonify(ok=True)

        except (KeyError, TypeError, ValueError):
            return jsonify(ok=False, reason="invalid port_bounds")

@app.route('/api/position')
@auth.login_required
def api_position():
    latestPosTupel = SensorData.query.filter_by(sensor="position") \
        .order_by(SensorData.zeitstempel.desc()) \
        .first()

    if not latestPosTupel:
        return {"ok": False}

    try:
        lat, lon = latestPosTupel.wert.split(",")
        # Python gitb dict zurueck und Flask wandelt in json um
        return {
            "ok": True,
            "lat": float(lat),
            "lon": float(lon),
            "timeRx": latestPosTupel.zeitstempel.isoformat()
        }
    except ValueError:
        return {"ok": False}



@app.route('/download/csv')
@auth.login_required
def download_csv():
    daten = SensorData.query.order_by(SensorData.zeitstempel.desc()).all()

    # CSV im Speicher erzeugen
    output = io.StringIO()
    writer = csv.writer(output, delimiter=';')

    # Header
    writer.writerow(["Sensor", "Wert", "Zeitstempel"])

    # Daten
    for d in daten:
        writer.writerow([
            d.sensor,
            d.wert,
            d.zeitstempel.strftime("%Y-%m-%d %H:%M:%S")
        ])

    # Response als Datei
    response = Response(
        output.getvalue(),
        mimetype="text/csv"
    )
    response.headers["Content-Disposition"] = \
        "attachment; filename=berta_logs.csv"

    return response

@app.route('/upload', methods=["GET"])
@auth.login_required
def upload():
    if "file" not in request.files:
        return "Keine Datei im Request", 400

    file = request.files["file"]

    if file.filename == "":
        return "Keine Datei ausgewählt", 400

    if not file.filename.lower().endswith(".json"):  # Nur .json erlauben
        return "Nur JSON-Dateien erlaubt", 400

    try: # Inhalt prüfen (echtes JSON?)
        data = json.load(file)
    except Exception:
        return "Ungültige JSON-Datei", 400

    with open(ESP_SETTINGS_PATH, "w") as f: # Datei speichern/ersetzen
        json.dump(data, f, indent=4)

    return redirect(url_for("dashboard"))  # oder index

# Server starten
if __name__ == '__main__':
    with app.app_context():
         db.create_all()  # Erstellt die Datenbanktabellen
    app.run(host='0.0.0.0', port=5000, debug=False)


