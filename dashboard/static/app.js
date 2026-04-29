const fields = {
  statusDot: document.getElementById("status-dot"),
  statusText: document.getElementById("status-text"),
  lastUpdate: document.getElementById("last-update"),
  ieqScore: document.getElementById("ieq-score"),
  iaqValue: document.getElementById("iaq-value"),
  iaqLabel: document.getElementById("iaq-label"),
  acousticIeq: document.getElementById("acoustic-ieq"),
  liveTemp: document.getElementById("live-temp"),
  liveHumidity: document.getElementById("live-humidity"),
  liveSoundDb: document.getElementById("live-sound-db"),
  livePm2p5: document.getElementById("live-pm2p5"),
  liveEco2: document.getElementById("live-eco2"),
  page2Eco2: document.getElementById("page2-eco2"),
  page2Bvoc: document.getElementById("page2-bvoc"),
  page2RawGas: document.getElementById("page2-raw-gas"),
  page3Accuracy: document.getElementById("page3-accuracy"),
  page3StabilizationStatus: document.getElementById("page3-stabilization-status"),
  page3CompGas: document.getElementById("page3-comp-gas"),
  page3GasPercentage: document.getElementById("page3-gas-percentage"),
  spsReady: document.getElementById("sps-ready"),
  spsError: document.getElementById("sps-error"),
  pm1p0: document.getElementById("pm1p0"),
  pm2p5: document.getElementById("pm2p5"),
  pm4p0: document.getElementById("pm4p0"),
  pm10p0: document.getElementById("pm10p0"),
};

const history = {
  temp: [],
  humidity: [],
  sound: [],
  pm25: [],
  eco2: [],
};

const charts = [
  { name: "temp", canvasId: "temp-chart", color: "#197a4b", fallbackMin: 18, fallbackMax: 32, unit: "C" },
  { name: "humidity", canvasId: "humidity-chart", color: "#087b83", fallbackMin: 20, fallbackMax: 80, unit: "%" },
  { name: "sound", canvasId: "sound-chart", color: "#0b7285", fallbackMin: 30, fallbackMax: 80, unit: "dB" },
  { name: "pm25", canvasId: "pm25-chart", color: "#b46a00", fallbackMin: 0, fallbackMax: 35, unit: "" },
  { name: "eco2", canvasId: "eco2-chart", color: "#b83232", fallbackMin: 350, fallbackMax: 1200, unit: "" },
];

let pollTimer = null;

function setConnectionStatus(className, label, detail) {
  if (fields.statusDot) fields.statusDot.className = className;
  if (fields.statusText) fields.statusText.textContent = label;
  if (fields.lastUpdate && detail) fields.lastUpdate.textContent = detail;
}

function showDashboardError(message, error) {
  console.error(message, error);
  setConnectionStatus("dot offline", "Dashboard error", message);
}

function findMissingFields() {
  return Object.entries(fields)
    .filter(([, element]) => !element)
    .map(([name]) => name);
}

function formatNumber(value, digits = 1) {
  if (value === null || value === undefined || Number.isNaN(Number(value))) {
    return "--";
  }

  return Number(value).toFixed(digits);
}

function numberOrNull(value) {
  if (value === null || value === undefined || value === "") return null;

  const parsed = Number(value);
  return Number.isNaN(parsed) ? null : parsed;
}

function firstNumber(...values) {
  for (const value of values) {
    const parsed = numberOrNull(value);
    if (parsed !== null) return parsed;
  }

  return null;
}

function formatTime(timestamp) {
  if (!timestamp) return "Waiting for data";
  return new Date(timestamp * 1000).toLocaleTimeString();
}

function iaqLabel(iaq) {
  if (iaq <= 50) return "Good";
  if (iaq <= 100) return "Fair";
  if (iaq <= 150) return "Light pollution";
  if (iaq <= 200) return "Polluted";
  if (iaq <= 250) return "Heavily polluted";
  return "Severe";
}

function sourceLabel(sample) {
  if (!sample.connected) return "Demo data";
  if (sample.source === "device") return "WiFi push";
  if (sample.source === "serial") return "Serial";
  return "Live device";
}

function readyLabel(value) {
  return value === true || value === "true" ? "Ready" : "Not ready";
}

function pushHistory(name, value) {
  if (value === null || value === undefined || Number.isNaN(Number(value))) return;

  history[name].push({
    value: Number(value),
    time: Date.now(),
  });

  if (history[name].length > 80) history[name].shift();
}

function niceRange(points, fallbackMin, fallbackMax) {
  if (points.length < 2) {
    return { min: fallbackMin, max: fallbackMax };
  }

  const values = points.map((point) => point.value);
  const rawMin = Math.min(...values);
  const rawMax = Math.max(...values);
  const padding = Math.max((rawMax - rawMin) * 0.18, 1);
  const min = Math.floor(rawMin - padding);
  const max = Math.ceil(rawMax + padding);

  return min === max ? { min: min - 1, max: max + 1 } : { min, max };
}

function formatAxisTime(time) {
  return new Date(time).toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit",
  });
}

function resizeCanvas(canvas) {
  const ratio = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  const width = Math.max(Math.floor(rect.width * ratio), 320);
  const height = Math.max(Math.floor(rect.height * ratio), 150);

  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }

  return ratio;
}

function drawChart(canvasId, points, options) {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return;

  const context = canvas.getContext("2d");
  const ratio = resizeCanvas(canvas);
  const { width, height } = canvas;
  const axisLeft = 52 * ratio;
  const axisBottom = 28 * ratio;
  const axisTop = 12 * ratio;
  const axisRight = 12 * ratio;
  const plotX = axisLeft;
  const plotY = axisTop;
  const plotWidth = width - axisLeft - axisRight;
  const plotHeight = height - axisTop - axisBottom;
  const ticks = 4;
  const { color, fallbackMin, fallbackMax, unit } = options;
  const { min, max } = niceRange(points, fallbackMin, fallbackMax);
  const range = Math.max(max - min, 1);

  context.clearRect(0, 0, width, height);
  context.font = `${11 * ratio}px Inter, system-ui, sans-serif`;
  context.lineWidth = 1 * ratio;
  context.strokeStyle = "#d9e0dc";
  context.fillStyle = "#65706b";
  context.textBaseline = "middle";

  for (let i = 0; i <= ticks; i += 1) {
    const y = plotY + (plotHeight / ticks) * i;
    const value = max - (range / ticks) * i;

    context.beginPath();
    context.moveTo(plotX, y);
    context.lineTo(plotX + plotWidth, y);
    context.stroke();

    context.textAlign = "right";
    context.fillText(`${value.toFixed(0)}${unit}`, plotX - 8 * ratio, y);
  }

  context.strokeStyle = "#9eaaa5";
  context.lineWidth = 1 * ratio;
  context.beginPath();
  context.moveTo(plotX, plotY);
  context.lineTo(plotX, plotY + plotHeight);
  context.lineTo(plotX + plotWidth, plotY + plotHeight);
  context.stroke();

  if (points.length) {
    const timeTicks = [...new Set([0, Math.floor((points.length - 1) / 2), points.length - 1])];
    context.textAlign = "center";
    context.textBaseline = "top";

    timeTicks.forEach((pointIndex) => {
      const point = points[pointIndex];
      const x = points.length === 1
        ? plotX + plotWidth
        : plotX + (pointIndex / (points.length - 1)) * plotWidth;

      context.fillText(formatAxisTime(point.time), x, plotY + plotHeight + 9 * ratio);
    });
  }

  if (points.length < 2) return;

  context.strokeStyle = color;
  context.lineWidth = 3 * ratio;
  context.lineJoin = "round";
  context.lineCap = "round";
  context.beginPath();

  points.forEach((point, index) => {
    const x = plotX + (index / (points.length - 1)) * plotWidth;
    const y = plotY + plotHeight - ((point.value - min) / range) * plotHeight;

    if (index === 0) {
      context.moveTo(x, y);
    } else {
      context.lineTo(x, y);
    }
  });

  context.stroke();
}

function drawAllCharts() {
  charts.forEach((chart) => {
    drawChart(chart.canvasId, history[chart.name], chart);
  });
}

function render(sample) {
  if (!sample) return;

  const bme = sample.bme || {};
  const sound = sample.sound || {};
  const ieq = sample.ieq || {};
  const sps30 = sample.sps30 || {};

  const iaq = numberOrNull(bme.iaq);
  const temp = firstNumber(bme.temp, bme.rawTemp);
  const humidity = firstNumber(bme.humidity, bme.rawHumidity);
  const db = numberOrNull(sound.db);
  const pm25 = numberOrNull(sps30.pm2p5);
  const eco2 = numberOrNull(bme.eco2);
  const spsReady = sps30.ready === true || sps30.ready === "true";

  setConnectionStatus(
    sample.connected ? "dot live" : "dot offline",
    sourceLabel(sample),
    `Updated ${formatTime(sample.timestamp)}`,
  );

  fields.ieqScore.textContent = formatNumber(ieq.score, 0);
  fields.iaqValue.textContent = formatNumber(iaq, 1);
  fields.iaqLabel.textContent = iaq === null ? "Waiting" : iaqLabel(iaq);
  fields.acousticIeq.textContent = formatNumber(db, 1);

  fields.liveTemp.textContent = formatNumber(temp, 1);
  fields.liveHumidity.textContent = formatNumber(humidity, 1);
  fields.liveSoundDb.textContent = formatNumber(db, 1);
  fields.livePm2p5.textContent = formatNumber(pm25, 1);
  fields.liveEco2.textContent = formatNumber(eco2, 0);

  fields.page2Eco2.textContent = formatNumber(eco2, 0);
  fields.page2Bvoc.textContent = formatNumber(bme.bvoc, 3);
  fields.page2RawGas.textContent = formatNumber(bme.rawGas, 0);
  fields.page3Accuracy.textContent = bme.accuracy === undefined || bme.accuracy === null ? "--" : `${bme.accuracy}/3`;
  fields.page3StabilizationStatus.textContent = formatNumber(bme.stabilizationStatus, 0);
  fields.page3CompGas.textContent = formatNumber(bme.compGas ?? bme.gas, 0);
  fields.page3GasPercentage.textContent = formatNumber(bme.gasPercentage, 1);

  fields.spsReady.textContent = readyLabel(spsReady);
  fields.spsError.textContent = sps30.error ?? "--";
  fields.pm1p0.textContent = formatNumber(sps30.pm1p0, 1);
  fields.pm2p5.textContent = formatNumber(sps30.pm2p5, 1);
  fields.pm4p0.textContent = formatNumber(sps30.pm4p0, 1);
  fields.pm10p0.textContent = formatNumber(sps30.pm10p0, 1);

  pushHistory("temp", temp);
  pushHistory("humidity", humidity);
  pushHistory("sound", db);
  pushHistory("pm25", pm25);
  pushHistory("eco2", eco2);
  drawAllCharts();
}

async function pollLatest() {
  try {
    const response = await fetch("/api/latest");
    if (!response.ok) throw new Error(`GET /api/latest ${response.status}`);
    render(await response.json());
  } catch (error) {
    console.error("Polling /api/latest failed", error);
    setConnectionStatus("dot offline", "Disconnected", "Waiting for data");
  }
}

function startPolling() {
  if (pollTimer) return;
  pollTimer = setInterval(pollLatest, 1000);
  pollLatest();
}

function startDashboard() {
  const missingFields = findMissingFields();
  if (missingFields.length) {
    showDashboardError(`Missing dashboard elements: ${missingFields.join(", ")}`);
    return;
  }

  startPolling();
}

window.addEventListener("resize", drawAllCharts);
startDashboard();
