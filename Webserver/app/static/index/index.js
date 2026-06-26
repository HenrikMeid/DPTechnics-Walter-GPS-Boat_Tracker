let numOfEntries = 10;

// Update of site contents
// ===================================================
async function ladeDaten() {
    const response = await fetch("/api/daten", {
                    method: "PUT",
                    headers: {"Content-Type": "application/json"},
                    body: JSON.stringify({reqNumEntries: numOfEntries})
                    });

    const json = await response.json();

    const tbody = document.getElementById("daten-body");
    tbody.innerHTML = "";

    json.daten.forEach(e => { // textContent prevents XSS over innerHTML
        const row = document.createElement("tr");
        
        const tdSensor = document.createElement("td");
        tdSensor.textContent = e.sensor;
        
        const tdWert = document.createElement("td");
        tdWert.textContent = e.wert;

        const tdZeitstempel = document.createElement("td");
        tdZeitstempel.textContent = e.zeit;

        row.appendChild(tdSensor);
        row.appendChild(tdWert);
        row.appendChild(tdZeitstempel);

        tbody.appendChild(row);
    });
}

console.log(isLive);
if (isLive) {
    ladeDaten(); // initial laden
    setInterval(ladeDaten, 5000); // alle 5 Sekunden
}

console.log("Tabelle geladen");

// Entry of time interval
// ===================================================
function parseTimestamp(text) {
    // Erwartet: yyyy-mm-dd hh:mm:ss
    return new Date(text.replace(" ", "T"));
}

async function getTableTimeRange() {
    const response = await fetch("/api/daten");
    const json = await response.json();

    const timeRxMin = json.info.timeRxMin;
    const timeRxMax = json.info.timeRxMax;
    const numOfEntrys = json.info.numOfEntrys;

    console.log("API min:", timeRxMin);
    console.log("API max:", timeRxMax);
    console.log("API count:", numOfEntrys);

    return {
        min: json.info.timeRxMin.slice(0,16),
        max: json.info.timeRxMax.slice(0,16),
        count: json.info.numOfEntrys
    };
}

console.log("Vor Ueberpruefung");
document.getElementById("zeitraum-form").addEventListener("submit", async function (e) {
    console.log("Ueberpruefung gestartet");
    const startInput = document.getElementById("start-date");
    const endInput = document.getElementById("end-date");

    const startDate = new Date(startInput.value);
    const endDate = new Date(endInput.value);
    console.log("starDate und endDate:" + startDate + " bis " + endDate);

    if (startDate > endDate) {
        alert("❌ Das Startdatum darf nicht nach dem Enddatum liegen.");
        e.preventDefault();
        return;
    }

    const range = await getTableTimeRange();
    if (!range) {
        alert("❌ Keine Zeitdaten in der Tabelle vorhanden.");
        e.preventDefault();
        return;
    }

    if (startDate < range.min || endDate > range.max) {
        alert(
            `❌ Zeitraum außerhalb der verfügbaren Daten!\n\n` +
            `Erlaubt:\n` +
            `${range.min.toISOString().slice(0,19).replace("T"," ")}\n` +
            `bis\n` +
            `${range.max.toISOString().slice(0,19).replace("T"," ")}`
        );
        e.preventDefault();
        return;
    }
});


console.log("Vor Handler");
document.addEventListener("DOMContentLoaded", async () => {
    console.log("Handler enterred");
    const dateRange = await getTableTimeRange();
    if (!dateRange) return;

    const start = document.getElementById("start-date");
    const end = document.getElementById("end-date");
    console.log(start + " bis " + end);

    // sperichert werde
    const savedStart = localStorage.getItem("startDate");
    const savedEnd = localStorage.getItem("endDate");

    if (savedStart) start.value = savedStart;
    if (savedEnd) end.value = savedEnd;

    start.min = dateRange.min;
    start.max = dateRange.max;

    end.min = dateRange.min;
    end.max = dateRange.max;
});

// Speichert eingaben
document.getElementById("start-date").addEventListener("change", saveDateInputs);
document.getElementById("end-date").addEventListener("change", saveDateInputs);

function saveDateInputs() {
    const start = document.getElementById("start-date").value;
    const end = document.getElementById("end-date").value;

    localStorage.setItem("startDate", start);
    localStorage.setItem("endDate", end);
}

// Reset button
document.getElementById("zeitraum-reset").addEventListener("click", async () => {
    const start = document.getElementById("start-date");
    const end = document.getElementById("end-date");

    start.value = "";
    end.value = "";

    localStorage.removeItem("startDate");
    localStorage.removeItem("endDate");

    await ladeDaten();

});

// Number of entries
document.getElementById("num_of_entries").addEventListener("change", async () => {
    numOfEntries = event.target.value;
    console.log("Gewählt:", numOfEntries);
});


// Download button and Options
// ===================================================
document.getElementById('download-button').addEventListener('click', async ()  => {
  
  window.location.href = '/download/csv';
});


