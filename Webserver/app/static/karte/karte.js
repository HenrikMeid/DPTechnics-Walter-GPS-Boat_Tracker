// Load site content from db
// ===================================================
// Sets startposition on map
let leafletMap;
let historyLine;
let marker;
let liveInterval;
let timeRx;
let pinPointMarker = null;
let markerId = 1;

document.addEventListener("DOMContentLoaded", () => {
    const mapDiv = document.getElementById("map");
    const lat = parseFloat(mapDiv.dataset.lat);
    const lon = parseFloat(mapDiv.dataset.lon);
    timeRx = new Date(mapDiv.dataset.timeRx + "Z");
    
    const readableTime = timeRx.toLocaleString("de-DE", { timeZone: "Europe/Berlin" });
    // Rendert Ortszeit unter Karte
    document.getElementById("time-value").textContent = readableTime;

    document.getElementById("googleMapsLink").href =`https://www.google.com/maps?q=${lat.toFixed(6)},${lon.toFixed(6)}`;
    document.getElementById("appleMapsLink").href =`https://maps.apple.com/?q=${lat.toFixed(6)},${lon.toFixed(6)}&q=Berta&z=15`;

    // Leaflet Objekt liegt alles unter L.*
    leafletMap = L.map("map").setView([lat, lon], 14);
   
    var OpenStreetMap = L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", { // Basis-Karte
        maxZoom: 20,
        attribution: '©openstreetmap'
    }).addTo(leafletMap);
    
    var OpenSeaMap = L.tileLayer("https://tiles.openseamap.org/seamark/{z}/{x}/{y}.png", { // OpenSeaMap Overlay
        maxZoom: 20,
        attribution: '©openstreetmap',
        transparent: true
    }).addTo(leafletMap);

    var EsriSatellite = L.tileLayer(
    "https://server.arcgisonline.com/ArcGIS/rest/services/" + "World_Imagery/MapServer/tile/{z}/{y}/{x}",{
        maxZoom: 20,
        attribution: "©openstreetmap"
    }
    );

    var baseMaps = {
    "OpenStreetMap": OpenStreetMap,
    "Satellit (ESRI)": EsriSatellite
    };

    var overlayMaps = {
        "OpenSeaMap": OpenSeaMap
    };

    L.control.layers(baseMaps, overlayMaps).addTo(leafletMap);
    // Port rectangle    
    var portBounds = [[0, 0], [0, 0]]; // define rectangle geographical bounds

    var rectangle = L.rectangle(portBounds, {color: "#ff7800", weight: 1}).addTo(leafletMap); // create an orange rectangle
    displayHarbour();
    
    // Icon based marker for position
    var logoIcon = L.icon({
    iconUrl: '/static/boat_military.png',

    iconSize:     [55, 55], // size of the icon
    iconAnchor:   [35, 35], // point of the icon which will correspond to marker's location
    popupAnchor:  [0, -15] // point from which the popup should open relative to the iconAnchor
    });

    marker = L.marker([lat, lon], {icon: logoIcon}).addTo(leafletMap);
    marker.bindPopup(`<b>Berta</b><br>
                    Ortszeit: ${readableTime}
                    `);
    // Polyline path object
    historyLine = L.polyline([], {
        color: "rgb(91, 84, 109)",
        weight: 4
    }).addTo(leafletMap);
    // Fullscreen element
    L.control.fullscreen({
        position: "bottomright",
        title: "Vollbild",
        titleCancel: "Vollbild verlassen"
    }).addTo(leafletMap);

    leafletMap.on("fullscreenchange", () => {
    leafletMap.invalidateSize();
    });

    leafletMap.setView([lat, lon], 14);

    // Reload each 5 Seconds
    console.log(isLive);
    if (isLive) {
        ladePosition();
        liveInterval = setInterval(ladePosition, 5000);
    }
    //Marker
    coloredMarker = L.icon({
        iconUrl: '/static/bouy_marker.png',
        iconSize: [25, 25],
        iconAnchor: [12, 25],
        popupAnchor: [1, -34]
    });
    // Setting Pinpointmarker
    leafletMap.on("click", (e) => {

        if (pinPointMarker) {
            pinPointMarker.closePopup();
            pinPointMarker.remove();
        }

        const { lat, lng } = e.latlng;
        
        pinPointMarker = L.marker([lat, lng], { icon: coloredMarker}).addTo(leafletMap);

        markerId = markerId+1;

        pinPointMarker.on("popupopen", () => {
            document.getElementById(`copyCoords_${markerId}`).addEventListener("click", async () => {
                const coordString = `${lat.toFixed(6)}, ${lng.toFixed(6)}`;

                try {
                    await navigator.clipboard.writeText(coordString);
                    document.getElementById(`copyCoords_${markerId}`).innerHTML = "kopiert ✔️"
                } catch (err) {
                    document.getElementById(`copyCoords_${markerId}`).innerHTML = "Fehler ❌"
                }
            });
            document.getElementById(`deleteMarker_${markerId}`).addEventListener("click", () => {
                pinPointMarker.closePopup();
                pinPointMarker.remove();
            });
        });

        pinPointMarker.on("popupclose", (e) => {
            pinPointMarker.remove();
        });


        pinPointMarker.bindPopup(`${lat.toFixed(6)}, ${lng.toFixed(6)} <br>
        <button class="button__marker" id="copyCoords_${markerId}">kopieren</button>
        <button class="button__marker" id="deleteMarker_${markerId}">löschen</button>`).openPopup();

    });

});

async function ladePosition() {
    try {
        const response = await fetch("/api/position");
        const data = await response.json();
        if (!data.ok) return;

        const newLat = data.lat;
        const newLon = data.lon;
        const newTime = new Date(data.timeRx  + "Z");
        
        const readableTime = newTime.toLocaleString("de-DE", { timeZone: "Europe/Berlin" });

        marker.setLatLng([newLat, newLon]);

        document.getElementById("lat-value").textContent = newLat.toFixed(6);
        document.getElementById("lon-value").textContent = newLon.toFixed(6);
        document.getElementById("time-value").textContent = readableTime;

        document.getElementById("googleMapsLink").href =`https://www.google.com/maps?q=${newLat.toFixed(6)},${newLon.toFixed(6)}`;
        document.getElementById("appleMapsLink").href =`https://maps.apple.com/?q=${newLat.toFixed(6)},${newLon.toFixed(6)}&q=Berta&z=15`;

        marker.setPopupContent(`<b>Berta</b><br>
                Ortszeit: ${readableTime}`);

        // Karte sanft nachfuehren
        if (!sessionStorage.getItem("map_initialized")) {
            leafletMap.setView([newLat, newLon], 12);
            sessionStorage.setItem("map_initialized", "true");
        }
    } catch (e) {
        console.error("Positions-Update fehlgeschlagen", e);
    }
}

// Display Harbour
// ===================================================
async function displayHarbour() {
    const response = await fetch("/api/daten");
    const jsonReponse = await response.json();
    const PORT_COORDINATES_RIGHT = jsonReponse.port_coordinates.port_bound_right
    const PORT_COORDINATES_LEFT = jsonReponse.port_coordinates.port_bound_left
    portBounds = [[PORT_COORDINATES_RIGHT.lat, PORT_COORDINATES_RIGHT.lon], 
                  [PORT_COORDINATES_LEFT.lat, PORT_COORDINATES_LEFT.lon]
                 ];
    rectangle = L.rectangle(portBounds, {color: "#ff7800", weight: 1}).addTo(leafletMap);
    rectangle.bindPopup("Bertas Hafen");
    return true;
}

// Trajectory calculation
// ===================================================
async function ladeTrack(startISO, endISO) {
    if (liveInterval) {
        clearInterval(liveInterval);
        liveInterval = null;
    }
    const url = `/api/track?start=${encodeURIComponent(startISO)}&end=${encodeURIComponent(endISO)}`;

    const response = await fetch(url);
    const data = await response.json();

    if (!data.ok || !data.track.length) {
        console.warn("Kein Track gefunden");
        return;
    }

    const latLngs = data.track.map(p => [p.lat, p.lon]);

    historyLine.setLatLngs(latLngs);
    leafletMap.fitBounds(historyLine.getBounds(), { padding: [20, 20] });

    totalDistance = data.totalDistance;
    document.getElementById("kilometers").textContent = totalDistance.toFixed(2) + " ";
}

// Entry of time interval
// ===================================================
function parseTimestamp(text) {
    // Erwartet: yyyy-mm-dd hh:mm:ss
    return new Date(text.replace(" ", "T"));
}

// gets the DB Infos vie get query
async function getTableTimeRange() {
    const response = await fetch("/api/daten");
    const jsonReponse = await response.json();

    const timeRxMin = jsonReponse.info.timeRxMin;
    const timeRxMax = jsonReponse.info.timeRxMax;
    const numOfEntrys = jsonReponse.info.numOfEntrys;
    const PORT_COORDINATES_RIGHT = jsonReponse.port_coordinates.port_bound_right
    const PORT_COORDINATES_LEFT = jsonReponse.port_coordinates.port_bound_left

    console.log("API min:", timeRxMin);
    console.log("API max:", timeRxMax);
    console.log("API count:", numOfEntrys);
    console.log("Port coordinates:", jsonReponse.port_coordinates);

    return {
        min: jsonReponse.info.timeRxMin.slice(0,16),
        max: jsonReponse.info.timeRxMax.slice(0,16),
        count: jsonReponse.info.numOfEntrys,
        port_coordinates_right: PORT_COORDINATES_RIGHT,
        port_coordinates_left: PORT_COORDINATES_LEFT
    };
}

function localInputToUTCISOString(value) {
    const [date, time] = value.split("T");
    const [y, m, d] = date.split("-").map(Number);
    const [hh, mm] = time.split(":").map(Number);

    // Achtung: Date.UTC → Monat -1
    return new Date(Date.UTC(y, m - 1, d, hh, mm)).toISOString();
}

console.log("Vor Ueberpruefung");
document.getElementById("zeitraum-form").addEventListener("submit", async function (e) {
    e.preventDefault();
    console.log("Ueberpruefung gestartet");

    const startInput = document.getElementById("start-date").value;
    const endInput   = document.getElementById("end-date").value;

    if (!startInput || !endInput) {
        alert("❌ Bitte Start- und Enddatum eingeben");
        return;
    }

    // 👉 NUR UTC verwenden
    const startISO = localInputToUTCISOString(startInput);
    const endISO   = localInputToUTCISOString(endInput);

    const startDate = new Date(startISO);
    const endDate   = new Date(endISO);

    console.log("startDate und endDate:", startDate.toISOString(), endDate.toISOString());


    if (startDate > endDate) {
        alert("❌ Das Startdatum darf nicht nach dem Enddatum liegen.");
        return;
    }

    const range = await getTableTimeRange();
    const minDate = new Date(range.min + "Z");
    const maxDate = new Date(range.max + "Z");

    if (startDate < minDate || endDate > maxDate) {
        alert(
            `❌ Zeitraum außerhalb der verfügbaren Daten!\n\n` +
            `Erlaubt:\n${range.min}\n bis\n${range.max}`
        );
        return;
    }

    ladeTrack(startISO, endISO);
});

// Save and display entry
console.log("Vor Handler");
document.addEventListener("DOMContentLoaded", async () => {
    console.log("Handler enterred");
    const dateRange = await getTableTimeRange();
    if (!dateRange) return;

    const start = document.getElementById("start-date");
    const end = document.getElementById("end-date");
    console.log(start + " bis " + end);

    // saves values
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
    const kilometers = document.getElementById("kilometers");

    start.value = "";
    end.value = "";
    kilometers.textContent = " - ";

    localStorage.removeItem("startDate");
    localStorage.removeItem("endDate");

    historyLine.setLatLngs([]);

    await ladePosition();

});

