let espIp = "192.168.1.100";
let fetchInterval = null;
let selectedChartSlot = 'A'; // Which slot is actively graphed

// The data buffer for CSV Export
// Structure: { timestamp, slotA: {v,c,cap,mode}, slotB: {...}, ... }
const historyData = [];

document.addEventListener("DOMContentLoaded", () => {
    // Load IP from local storage if exists
    const savedIp = localStorage.getItem("espIp");
    if (savedIp) {
        document.getElementById("esp-ip").value = savedIp;
        espIp = savedIp;
    }

    document.getElementById("connect-btn").addEventListener("click", () => {
        espIp = document.getElementById("esp-ip").value.trim();
        localStorage.setItem("espIp", espIp);
        startFetching();
        showToast("Connecting to " + espIp);
    });
    
    document.getElementById("export-csv-btn").addEventListener("click", exportCSV);

    initChart();

    // Auto-start if IP is set
    startFetching();
});

function startFetching() {
    if (fetchInterval) clearInterval(fetchInterval);
    fetchData(); // Fetch immediately
    fetchInterval = setInterval(fetchData, 2000); // then every 2 seconds
}

async function fetchData() {
    try {
        const res = await fetch(`http://${espIp}/status`, { cache: "no-store", mode: "cors" });
        if (!res.ok) throw new Error("Network response was not ok");
        const data = await res.json();
        
        // Data is an array of 4 slots: [{slot:"A", ...}, {slot:"B", ...}, ...]
        const record = {
            timestamp: new Date().toISOString()
        };
        const timeLabel = new Date().toLocaleTimeString([], {hour: '2-digit', minute:'2-digit', second:'2-digit'});
        let aV=null, bV=null, cV=null, dV=null;

        data.forEach(slotData => {
            updateSlotUI(slotData);
            record[`slot${slotData.slot}`] = slotData;
            if(slotData.slot === 'A') aV = slotData.voltage;
            if(slotData.slot === 'B') bV = slotData.voltage;
            if(slotData.slot === 'C') cV = slotData.voltage;
            if(slotData.slot === 'D') dV = slotData.voltage;
        });

        // Add to history
        historyData.push(record);

        // Update live chart for the currently selected slot
        const selectedSlotData = record[`slot${selectedChartSlot}`];
        if (selectedSlotData) {
            updateChart(timeLabel, selectedSlotData.voltage, selectedSlotData.current, selectedSlotData.capacity);
        }

    } catch (e) {
        console.warn("Fetch failed, is ESP32 on?", e);
    }
}

function updateSlotUI(data) {
    const { slot, battery_num, voltage, current, capacity, mode, cycle_current, cycle_target } = data;
    
    // Update text
    document.getElementById(`v-${slot}`).innerHTML = `${voltage.toFixed(2)} <small>V</small>`;
    document.getElementById(`c-${slot}`).innerHTML = `${Math.floor(current)} <small>mA</small>`;
    document.getElementById(`cap-${slot}`).innerHTML = `${capacity.toFixed(3)} <small>mAh</small>`;
    
    // Only update batt input placeholder to not overwrite user typing
    const battInput = document.getElementById(`batt-${slot}`);
    if (battInput && document.activeElement !== battInput) {
        if (battery_num > 0 && !battInput.value) {
           battInput.placeholder = battery_num;
        } else if (battery_num > 0) {
           battInput.placeholder = battery_num;
        }
    }

    // Update Mode Badge and Card Border
    const card = document.getElementById(`slot-${slot}`);
    const badge = document.getElementById(`badge-${slot}`);
    
    // Cycle modes start with "CYCLE"
    let displayMode = mode;
    if (mode.startsWith("CYCLE")) {
        displayMode = "CYCLE"; // keep border color as purple based on prefix
    }
    
    card.setAttribute("data-mode", displayMode);
    badge.innerText = mode; // show full string (CYCLE - CHARGING)

    // Cycle info
    const cycleInfo = document.getElementById(`cycle-info-${slot}`);
    if (displayMode === "CYCLE") {
        cycleInfo.classList.remove("hidden");
        document.getElementById(`cycle-cnt-${slot}`).innerText = cycle_current;
        document.getElementById(`cycle-tgt-${slot}`).innerText = cycle_target;
    } else {
        cycleInfo.classList.add("hidden");
    }
}

async function sendCommand(cmd, slot) {
    const payload = { command: cmd, slot: slot };
    await postToESP(payload);
    let cmdName = cmd === '1'? 'Charge' : cmd === '2'? 'Discharge' : 'Stop';
    showToast(`Command ${cmdName} sent to Slot ${slot}`);
}

function toggleCycleInput(slot) {
    const sel = document.getElementById(`mode-sel-${slot}`);
    const input = document.getElementById(`cycles-${slot}`);
    if (sel.value === '3') {
        input.classList.remove('hidden');
    } else {
        input.classList.add('hidden');
    }
}

async function startSlot(slot) {
    const sel = document.getElementById(`mode-sel-${slot}`);
    const cmd = sel.value;
    
    if (cmd === '3') {
        sendCycle(slot);
    } else {
        sendCommand(cmd, slot);
    }
}

async function sendCycle(slot) {
    const cycles = parseInt(document.getElementById(`cycles-${slot}`).value) || 3;
    const payload = { command: '3', slot: slot, cycles: cycles };
    await postToESP(payload);
    showToast(`Cycle test (${cycles}x) started on Slot ${slot}`);
}

async function setBattNum(slot) {
    const input = document.getElementById(`batt-${slot}`);
    const num = parseInt(input.value);
    if (!num) return alert("Enter a valid battery number");
    
    const payload = { command: '4', slot: slot, battery_number: num };
    await postToESP(payload);
    showToast(`Battery # set to ${num} for Slot ${slot}`);
    input.value = ''; // clear input
    input.placeholder = num;
}

async function postToESP(payload) {
    try {
        await fetch(`http://${espIp}/command`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload),
            mode: "cors"
        });
        // Speed up the update to reflect the new state immediately
        setTimeout(fetchData, 200);
    } catch (e) {
        console.error("Failed to send command", e);
        showToast("Error: Failed to connect to ESP32", true);
    }
}

function showToast(msg, isError = false) {
    const toast = document.getElementById("toast");
    toast.innerText = msg;
    toast.style.background = isError ? "#f85149" : "#2ea043";
    toast.classList.remove("hidden");
    setTimeout(() => toast.classList.add("hidden"), 3000);
}

// ==== CSV EXPORT ====
function exportCSV() {
    if (historyData.length === 0) {
        return alert("No data recorded yet. Wait for a connection.");
    }
    
    // Headers: Date & Time, Status, Batt. No., Elapsed Time (s), Voltage (V), Current (mA), Simpson Capacity (mAh)
    let csv = "Date & Time,Status,Batt. No.,Elapsed Time (s),Voltage (V),Current (mA),Simpson Capacity (mAh)\n";

    historyData.forEach(row => {
        const slotData = row[`slot${selectedChartSlot}`];
        if (slotData) {
            // Format time seamlessly as YYYY-MM-DD HH:MM:SS
            const dateObj = new Date(row.timestamp);
            const timeStr = dateObj.toLocaleString('en-US', { 
                year: 'numeric', month: '2-digit', day: '2-digit', 
                hour: '12-digit', minute:'2-digit', second:'2-digit', hour12: false
            }).replace(',', '');
            
            const r = [
                timeStr,
                slotData.mode,
                slotData.battery_num,
                slotData.elapsed || 0,
                slotData.voltage.toFixed(3),
                slotData.current.toFixed(0),
                slotData.capacity.toFixed(4)
            ];
            csv += r.join(",") + "\n";
        }
    });

    const blob = new Blob([csv], { type: 'text/csv' });
    const url = window.URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.setAttribute('href', url);
    a.setAttribute('download', `slot_${selectedChartSlot}_battery_log_${new Date().getTime()}.csv`);
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    window.URL.revokeObjectURL(url);
    showToast(`Slot ${selectedChartSlot} CSV Downloaded!`);
}

// ==== CHART LOGIC ====
function selectSlot(slotStr) {
    if(selectedChartSlot === slotStr) return; // already selected
    
    // Update UI active states
    document.querySelectorAll('.slot-card').forEach(el => el.classList.remove('active-slot'));
    document.getElementById(`slot-${slotStr}`).classList.add('active-slot');
    document.getElementById('chart-title-slot').innerText = `(Slot ${slotStr})`;
    
    selectedChartSlot = slotStr;
    
    // Retrospectively populate chart with the selected slot's history
    const labels = [];
    const vData = [];
    const cData = [];
    const capData = [];
    
    // Grab the last 120 points max
    const sliceStart = Math.max(0, historyData.length - 120);
    for (let i = sliceStart; i < historyData.length; i++) {
        const row = historyData[i];
        const timeStr = new Date(row.timestamp).toLocaleTimeString([], {hour: '2-digit', minute:'2-digit', second:'2-digit'});
        labels.push(timeStr);
        const slData = row[`slot${selectedChartSlot}`];
        if (slData) {
            vData.push(slData.voltage);
            cData.push(slData.current);
            capData.push(slData.capacity);
        } else {
            vData.push(null);
            cData.push(null);
            capData.push(null);
        }
    }
    
    if (liveChart) {
        liveChart.data.labels = labels;
        liveChart.data.datasets[0].data = vData;
        liveChart.data.datasets[1].data = cData;
        liveChart.data.datasets[2].data = capData;
        liveChart.update();
    }
}

function initChart() {
    const ctx = document.getElementById('liveChart').getContext('2d');
    
    Chart.defaults.color = '#8b949e';
    Chart.defaults.font.family = 'Inter';
    
    liveChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [], 
            datasets: [
                { label: 'Voltage (V)', borderColor: '#58a6ff', backgroundColor: 'rgba(88, 166, 255, 0.1)', data: [], tension: 0.4, pointRadius: 0, borderWidth: 2, yAxisID: 'y' },
                { label: 'Current (mA)', borderColor: '#f85149', backgroundColor: 'rgba(248, 81, 73, 0.1)', data: [], tension: 0.4, pointRadius: 0, borderWidth: 2, yAxisID: 'y1' },
                { label: 'Simpson Cap (mAh)', borderColor: '#a371f7', backgroundColor: 'rgba(163, 113, 247, 0.1)', data: [], tension: 0.4, pointRadius: 0, borderWidth: 3, borderDash: [5, 5], yAxisID: 'y' }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            interaction: { mode: 'index', intersect: false },
            plugins: {
                legend: { position: 'top', labels: { usePointStyle: true, boxWidth: 8 } },
                tooltip: {
                    callbacks: {
                        label: function(context) {
                            let label = context.dataset.label || '';
                            if (label) {
                                label += ': ';
                            }
                            if (context.parsed.y !== null) {
                                label += context.parsed.y;
                            }
                            return label;
                        }
                    }
                }
            },
            scales: {
                x: {
                    grid: { color: 'rgba(255,255,255,0.05)' },
                    ticks: { maxTicksLimit: 10 }
                },
                y: {
                    type: 'linear',
                    display: true,
                    position: 'left',
                    grid: { color: 'rgba(255,255,255,0.05)' },
                    title: { display: true, text: 'Voltage / mAh' }
                },
                y1: {
                    type: 'linear',
                    display: true,
                    position: 'right',
                    grid: { drawOnChartArea: false },
                    title: { display: true, text: 'Current (mA)' }
                }
            }
        }
    });
}

function updateChart(timeLabel, vVal, cVal, capVal) {
    if (!liveChart) return;
    
    liveChart.data.labels.push(timeLabel);
    liveChart.data.datasets[0].data.push(vVal);
    liveChart.data.datasets[1].data.push(cVal);
    liveChart.data.datasets[2].data.push(capVal);
    
    // Keep last 120 points
    if (liveChart.data.labels.length > 120) {
        liveChart.data.labels.shift();
        liveChart.data.datasets.forEach(d => d.data.shift());
    }
    
    liveChart.update('none');
}

