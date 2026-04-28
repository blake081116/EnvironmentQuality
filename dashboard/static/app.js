const fields = {
  statusDot: document.getElementById("status-dot"),
  statusText: document.getElementById("status-text"),
  lastUpdate: document.getElementById("last-update"),
  ieqScore: document.getElementById("ieq-score"),
  iaqValue: document.getElementById("iaq-value"),
  iaqLabel: document.getElementById("iaq-label"),
  soundDb: document.getElementById("sound-db"),
  temp: document.getElementById("temp"),
  humidity: document.getElementById("humidity"),
  pressure: document.getElementById("pressure"),
  accuracy: document.getElementById("accuracy"),
  eco2: document.getElementById("eco2"),
  bvoc: document.getElementById("bvoc"),
  gas: document.getElementById("gas"),
  pauseButton: document.getElementById("pause-button"),
};

const history = {
  iaq: [],
  sound: [],
};

let paused = false;
let source = null;

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
  const height = Math.max(Math.floor(rect.height * ratio), 170);

  if (canvas.width !== width || canvas.height !== height) {
    canvas.width = width;
    canvas.height = height;
  }

  return ratio;
}

function drawChart(canvasId, points, options) {
  const canvas = document.getElementById(canvasId);
  const context = canvas.getContext("2d");
  const ratio = resizeCanvas(canvas);
  const { width, height } = canvas;
  const axisLeft = 54 * ratio;
  const axisBottom = 30 * ratio;
  const axisTop = 14 * ratio;
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

function render(sample) {
  if (paused || !sample) return;

  const bme = sample.bme || {};
  const sound = sample.sound || {};
  const ieq = sample.ieq || {};

  const iaq = numberOrNull(bme.iaq);
  const db = numberOrNull(sound.db);

  fields.statusDot.className = sample.connected ? "dot live" : "dot offline";
  fields.statusText.textContent = sample.connected ? "Live device" : "Demo data";
  fields.lastUpdate.textContent = `Updated ${formatTime(sample.timestamp)}`;

  fields.ieqScore.textContent = formatNumber(ieq.score, 0);
  fields.iaqValue.textContent = formatNumber(iaq, 1);
  fields.iaqLabel.textContent = iaq === null ? "Waiting" : iaqLabel(iaq);
  fields.soundDb.textContent = formatNumber(db, 1);

  fields.temp.textContent = formatNumber(bme.temp, 1);
  fields.humidity.textContent = formatNumber(bme.humidity, 1);
  fields.pressure.textContent = formatNumber(bme.pressure, 1);
  fields.accuracy.textContent = bme.accuracy ?? "--";
  fields.eco2.textContent = formatNumber(bme.eco2, 0);
  fields.bvoc.textContent = formatNumber(bme.bvoc, 3);
  fields.gas.textContent = formatNumber(bme.gas, 0);

  pushHistory("iaq", iaq);
  pushHistory("sound", db);
  drawChart("iaq-chart", history.iaq, {
    color: "#197a4b",
    fallbackMin: 0,
    fallbackMax: 100,
    unit: "",
  });
  drawChart("sound-chart", history.sound, {
    color: "#087b83",
    fallbackMin: 30,
    fallbackMax: 80,
    unit: "dB",
  });
}

async function pollLatest() {
  try {
    const response = await fetch("/api/latest");
    render(await response.json());
  } catch {
    fields.statusDot.className = "dot offline";
    fields.statusText.textContent = "Disconnected";
  }
}

function startStream() {
  if (!window.EventSource) {
    setInterval(pollLatest, 1000);
    return;
  }

  source = new EventSource("/api/stream");
  source.onmessage = (event) => render(JSON.parse(event.data));
  source.onerror = () => {
    if (source) source.close();
    source = null;
    setInterval(pollLatest, 1000);
  };
}

fields.pauseButton.addEventListener("click", () => {
  paused = !paused;
  fields.pauseButton.textContent = paused ? "Resume" : "Pause";
});

startStream();
