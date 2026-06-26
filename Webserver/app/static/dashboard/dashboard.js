// Sets startposition on map
let leafletMap;
let portBounds;
let portBoundsMarker;
let selectedPoints = [];
let marker
let markers = [];
let coloredMarker;
let rectangle = null;
let hafenBearbeiten;
let mapDiv;
let sparkScript = null;
let numOfEntries;

document.addEventListener("DOMContentLoaded", async () => {
    // Leaflet Objekt liegt alles unter L.*
    leafletMap = L.map("map--selector").setView([47.619641, 9.397131], 10);
   
    var OpenStreetMap = L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", { // Basis-Karte
        maxZoom: 20,
        attribution: '©openstreetmap'
    }).addTo(leafletMap);
    
    var OpenSeaMap = L.tileLayer("https://tiles.openseamap.org/seamark/{z}/{x}/{y}.png", { // OpenSeaMap Overlay
        maxZoom: 20,
        attribution: '',
        transparent: true
    }).addTo(leafletMap);

    var EsriSatellite = L.tileLayer(
    "https://server.arcgisonline.com/ArcGIS/rest/services/" + "World_Imagery/MapServer/tile/{z}/{y}/{x}",{
        maxZoom: 20,
        attribution: ""
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

    // Fullscreen element
    L.control.fullscreen({
        position: "bottomright",
        title: "Vollbild",
        titleCancel: "Vollbild verlassen"
    }).addTo(leafletMap);

    leafletMap.on("fullscreenchange", () => {
    leafletMap.invalidateSize();
    });

    //Marker
    coloredMarker = L.icon({
        iconUrl: '/static/bouy_marker.png',
        iconSize: [25, 25],
        iconAnchor: [12, 25],
        popupAnchor: [1, -34]
    });

    await displayHarbour();

    hafenBearbeiten = document.getElementById("hafen_bearbeiten");
    mapDiv = document.getElementById("map--selector");

    hafenBearbeiten.addEventListener("change", () => {

        if (hafenBearbeiten.checked) {
            mapDiv.style.boxShadow = "0 4px 10px rgba(228, 87, 51, 0.9)";
        } else {
            mapDiv.style.boxShadow = "0 4px 10px rgba(0, 0, 0, 0.15)";
        }
    });

    // Click handler
    leafletMap.on("click", (e) => {
        
        if (hafenBearbeiten.checked) {

            const { lat, lng } = e.latlng;

            selectedPoints.push([lat, lng]);

            marker = L.marker(e.latlng, { icon: coloredMarker });
            markers.push(marker);
            marker = null;
            markers.forEach(m => {m.addTo(leafletMap)});

            if (selectedPoints.length === 2) {
                createPortBounds();
                hafenBearbeiten.checked = false;
                mapDiv.style.boxShadow = "0 4px 10px rgba(0, 0, 0, 0.15)";
            }
        }
    });

    // Pumpen status
    await loadEspStatus();
    setInterval(loadEspStatus, 5000);
    await loadPumpStatus();
    setInterval(loadPumpStatus, 5000);


})

document.getElementById("esp_reset").addEventListener("change", () => {
    if (document.getElementById("esp_reset").checked) {
        alert("Reset failed: Not yet implemented");
    }
})

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
    
    markers.forEach(m => m.remove());
    markers = [];

    portBounds.forEach(coords => {
        marker = L.marker(coords, { icon: coloredMarker });
        markers.push(marker); 
    });

    markers.forEach(m => {m.addTo(leafletMap)});

    leafletMap.fitBounds(portBounds, {
        padding: [50, 50]
    });

    document.getElementById("p_nw").textContent = `${PORT_COORDINATES_RIGHT.lat.toFixed(6)}, ${PORT_COORDINATES_RIGHT.lon.toFixed(6)}`;
    document.getElementById("p_so").textContent = `${PORT_COORDINATES_LEFT.lat.toFixed(6)}, ${PORT_COORDINATES_LEFT.lon.toFixed(6)}`;

    return true;
}

async function createPortBounds() {

    markers.forEach(m => m.remove());
    markers = [];

    const p1 = selectedPoints[0];
    const p2 = selectedPoints[1];

    const northWest = [
        Math.max(p1[0], p2[0]),
        Math.min(p1[1], p2[1])
    ];

    const southEast = [
        Math.min(p1[0], p2[0]),
        Math.max(p1[1], p2[1])
    ];

    portBounds = { northWest, southEast };

    portBoundsMarker = [[northWest[0], northWest[1]], 
                        [southEast[0], southEast[1]]
                        ];
    
    portBoundsMarker.forEach(coords => {
        marker = L.marker(coords, { icon: coloredMarker });
        markers.push(marker); 
    });
    markers.forEach(m => m.addTo(leafletMap));

    //portBoundsMarker.map(coords => L.marker(coords, { icon: coloredMarker }).addTo(leafletMap));

    const leafletBounds = [
        [southEast[0], northWest[1]], // SW
        [northWest[0], southEast[1]]  // NE
    ];

    if (rectangle) leafletMap.removeLayer(rectangle);

    rectangle = L.rectangle(leafletBounds, {
        color: "#ff7800",
        weight: 2
    }).addTo(leafletMap);

    rectangle.bindPopup("Neuer Hafen").openPopup();

    leafletMap.fitBounds(leafletBounds, {
        padding: [50, 50]
    });

    document.getElementById("p_nw").textContent = `${northWest[0].toFixed(6)}, ${northWest[1].toFixed(6)}`;
    document.getElementById("p_so").textContent = `${southEast[0].toFixed(6)}, ${southEast[1].toFixed(6)}`;

    selectedPoints = []; // Reset

    console.log("Neue portBounds:", portBounds);

    try {
        const response = await fetch("/api/daten", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                port_bounds: {
                    north_west: { lat: northWest[0], lon: northWest[1] },
                    south_east: { lat: southEast[0], lon: southEast[1] }
                }
            })
        });

        if (!response.ok) throw new Error(`HTTP ${response.status}`);

        console.log("PortBounds erfolgreich gespeichert");

    } catch (err) {
        console.error("Fehler beim Speichern der PortBounds:", err);
    }
}

async function loadEspStatus() {
    const response = await fetch("/api/daten");
    const json = await response.json();

    const timeRxMaxIso = json.info.timeRxMax;

    if (timeRxMaxIso) {
        const isOlderThan20Min = (new Date() - new Date(json.info.timeRxMax)) > 20 * 60 * 1000;

        if (isOlderThan20Min) {
            document.getElementById("esp_status").innerHTML = "⚪️";
            document.getElementById("esp_label").setAttribute("title", "dead");
        } else {
            document.getElementById("esp_status").innerHTML = "🟢";
            document.getElementById("esp_label").setAttribute("title", "alive");
        }
    }
}


async function loadPumpStatus() {
        const response = await fetch("/dashboard", {
                    method: "PUT",
                    headers: {"Content-Type": "application/json"},
                    });

    const json = await response.json();

    if (json.pumpState === true) {
        document.getElementById("pumpe_status").innerHTML = "🟢"; // alternativ "\u{1F7E2}"
        document.getElementById("pumpe_label").setAttribute("title", "läuft"); // alternativ "\u{1F7E2}"
    } else {
        document.getElementById("pumpe_status").innerHTML = "⚪"; // alternativ "\u{1F7E2}"
        document.getElementById("pumpe_label").setAttribute("title", "gestoppt"); // alternativ "\u{1F7E2}" 
    }

}


