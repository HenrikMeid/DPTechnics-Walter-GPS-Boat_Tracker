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
from bertaSetup import *

ESP_SETTINGS_PATH = "esp_settings.json"
# SETTINGS_PATH = "settings.json"
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
SETTINGS_PATH = os.path.join(BASE_DIR, "settings.json")
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

BASE_DIR = os.path.abspath(os.path.dirname(__file__))
LOG_DIR = os.path.join(BASE_DIR, "logs")

os.makedirs(LOG_DIR, exist_ok=True)

handler2 = logging.FileHandler(
    os.path.join(LOG_DIR, "app.log")
)

logging.basicConfig(
    level=logging.INFO,
    handlers=[handler, handler2],
    format="%(asctime)s [%(levelname)s] %(message)s"
)

app.logger.handlers = [] # flask -> gunicorn -> docker
app.logger.addHandler(handler)
app.logger.setLevel(logging.INFO)
app.logger.propagate = False

users = setup_users(SETTINGS_PATH)

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

from routes import *

#@auth.verify_password
#def verify_password(username, password):
#    app.logger.info(f"Users available: {users.keys()}")
#    app.logger.info(f"Login attempt: {username}")
#
#    if username in users and check_password_hash(users[username], password):
#        return username
#
#    return None

# Server starten
if __name__ == '__main__':
    with app.app_context():
         db.create_all()  # Erstellt die Datenbanktabellen
    app.run(host='0.0.0.0', port=5000, debug=False)


