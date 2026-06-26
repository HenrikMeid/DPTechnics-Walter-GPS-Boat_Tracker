FROM python:3.11-slim

WORKDIR /app

COPY app/requirements.txt .
RUN python -m pip install --upgrade pip
RUN python -m pip install --no-cache-dir -r requirements.txt

COPY app/ .

CMD ["gunicorn", "--workers=2", "--threads=4", "--timeout=120", "-b", "0.0.0.0:5000", "server:app"]


