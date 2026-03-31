import tkinter as tk
from tkinter import ttk, scrolledtext, filedialog
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
from mpl_toolkits.mplot3d import Axes3D
import numpy as np
import socket
import threading
import json
import time
from datetime import datetime
import queue
import csv

class WiFiBatteryMonitor:
    def __init__(self, root):
        self.root = root
        self.root.title("Battery Analysis Suite v1.0 - With Simpson Capacity")
        self.root.geometry("1600x900")
        self.root.configure(bg="#1e1e2f")

        style = ttk.Style()
        style.theme_use("clam")

        self.bg = "#1e1e2f"
        self.panel = "#2b2b3c"
        self.accent = "#00d4ff"
        self.text = "#ffffff"
        self.font_title = ("Segoe UI", 14, "bold")
        self.font_normal = ("Segoe UI", 10)

        # Networking
        self.socket = None
        self.connected = False
        self.recording = True
        self.data_queue = queue.Queue()
        self.esp32_ip = tk.StringVar(value="10.132.121.93")
        self.esp32_port = 8888
        self.start_time = time.time()
        
        # Animation control
        self.animation_speed = 100  # ms
        self.max_points = 1000

        # Colors for each slot
        self.colors = {
            "A": {"voltage": "Orange", "current": "blue", "capacity": "lime"},
            "B": {"voltage": "yellow", "current": "orange", "capacity": "black"},
            "C": {"voltage": "lime", "current": "red", "capacity": "green"},
            "D": {"voltage": "blue", "current": "gold", "capacity": "aqua"},
        }

        # Battery numbers storage
        self.battery_numbers = {"A": 0, "B": 0, "C": 0, "D": 0}

        # Data storage with Simpson capacity
        self.slots = {}
        for s in "ABCD":
            self.slots[s] = {
                "time":[],
                "voltage":[],
                "current":[],
                "simpson_capacity": 0,  # Simpson's 1/3 rule capacity
                "capacity":[],  # Store capacity over time
                "mode": "IDLE",
                "cycle_current": 0,
                "cycle_target": 0
            }

        # Zoom settings for each graph
        self.zoom_levels = {}
        for s in "ABCD":
            self.zoom_levels[s] = {
                "x_range": None,  # Will store (min, max) when zoomed
                "y1_range": None, # Voltage
                "y2_range": None, # Current
                "y3_range": None  # Capacity
            }

        self.setup_gui()
        self.update_display()

    # ================= GUI =================
    def setup_gui(self):
        main = ttk.Frame(self.root)
        main.pack(fill=tk.BOTH, expand=True)

        left = ttk.Frame(main, width=300)
        left.pack(side=tk.LEFT, fill=tk.Y, padx=5, pady=5)

        # Connection Control
        control = ttk.LabelFrame(left, text="Connection Control", padding=10)
        control.pack(fill=tk.X, padx=5, pady=5)

        ttk.Label(control, text="ESP32 IP").pack(anchor="w")
        ttk.Entry(control, textvariable=self.esp32_ip).pack(fill=tk.X, pady=3)
        ttk.Button(control, text="Connect", command=self.connect_wifi).pack(fill=tk.X, pady=2)
        ttk.Button(control, text="Disconnect", command=self.disconnect_wifi).pack(fill=tk.X, pady=2)

        self.status_label = ttk.Label(control, text="● Disconnected", foreground="red")
        self.status_label.pack(pady=5)

        # Live Measurements
        live = ttk.LabelFrame(left, text="Live Measurements", padding=10)
        live.pack(fill=tk.X, padx=5, pady=5)

        self.live_labels = {}
        for s in "ABCD":
            frame = ttk.Frame(live)
            frame.pack(fill=tk.X, pady=2)
            
            # Slot indicator with color
            color_label = tk.Label(frame, text=f"Slot {s}", 
                                 bg=self.colors[s]["voltage"], 
                                 fg="black", width=8, font=("Segoe UI", 9, "bold"))
            color_label.pack(side=tk.LEFT, padx=2)
            
            lbl = ttk.Label(frame, text="No Batt# | --- V | --- mA | --- mAh | ---")
            lbl.pack(side=tk.LEFT, padx=5)
            self.live_labels[s] = lbl

        # Data Files
        files = ttk.LabelFrame(left, text="Data Files", padding=10)
        files.pack(fill=tk.X, padx=5, pady=5)
        ttk.Button(files, text="Load History CSV", command=self.load_history).pack(fill=tk.X, pady=2)
        ttk.Button(files, text="Export CSV", command=self.export_all).pack(fill=tk.X, pady=2)
        ttk.Button(files, text="Clear All Data", command=self.clear_all_data).pack(fill=tk.X, pady=2)

        # System Log
        log_frame = ttk.LabelFrame(left, text="System Log", padding=10)
        log_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        self.log_text = scrolledtext.ScrolledText(log_frame, height=15)
        self.log_text.pack(fill=tk.BOTH, expand=True)

        # Center Panel with Notebook
        center = ttk.Frame(main)
        center.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5, pady=5)

        self.notebook = ttk.Notebook(center)
        self.notebook.pack(fill=tk.BOTH, expand=True)

        # Create tabs for each slot
        self.graph_frames = {}
        for s in "ABCD":
            self.create_slot_tab(s)

        # Create statistics tab
        self.create_stats_tab()
        
        # Create 3D tab
        self.create_3d_tab()

    # ================= SLOT TABS =================
    def create_slot_tab(self, slot):
        """Create individual tab for each battery slot with Voltage, Current, and Simpson Capacity"""
        frame = ttk.Frame(self.notebook)
        self.notebook.add(frame, text=f"Slot {slot}")

        # Create toolbar frame
        toolbar_frame = ttk.Frame(frame)
        toolbar_frame.pack(fill=tk.X, padx=5, pady=2)

        # Add zoom control buttons
        zoom_frame = ttk.LabelFrame(toolbar_frame, text="Zoom Controls", padding=5)
        zoom_frame.pack(side=tk.LEFT, padx=5)

        ttk.Button(zoom_frame, text="Zoom Out All", 
                  command=lambda: self.zoom_out_all(slot)).pack(side=tk.LEFT, padx=2)
        ttk.Button(zoom_frame, text="Zoom Out X", 
                  command=lambda: self.zoom_out_axis(slot, 'x')).pack(side=tk.LEFT, padx=2)
        ttk.Button(zoom_frame, text="Zoom Out Y", 
                  command=lambda: self.zoom_out_axis(slot, 'y')).pack(side=tk.LEFT, padx=2)
        ttk.Button(zoom_frame, text="Reset View", 
                  command=lambda: self.reset_view(slot)).pack(side=tk.LEFT, padx=2)

        # Create figure with 3 subplots (Voltage, Current, Capacity)
        fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(8, 7))
        fig.tight_layout(pad=4)
        fig.patch.set_facecolor('#f0f0f0')

        # Style the axes
        for ax in [ax1, ax2, ax3]:
            ax.set_facecolor('#ffffff')
            ax.grid(True, linestyle='--', alpha=0.7, color='#cccccc')
            ax.spines['top'].set_visible(False)
            ax.spines['right'].set_visible(False)
            ax.tick_params(colors='#333333')
            ax.xaxis.label.set_color('#333333')
            ax.yaxis.label.set_color('#333333')

        # Create canvas and toolbar
        canvas = FigureCanvasTkAgg(fig, master=frame)
        toolbar = NavigationToolbar2Tk(canvas, frame)
        toolbar.update()
        canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        # Initialize empty lines
        voltage_line, = ax1.plot([], [], 
                                 color=self.colors[slot]["voltage"], 
                                 linewidth=2.5, 
                                 marker='o', 
                                 markersize=4,
                                 markevery=5,
                                 label='Voltage')
        
        current_line, = ax2.plot([], [], 
                                 color=self.colors[slot]["current"], 
                                 linewidth=2.5, 
                                 marker='s', 
                                 markersize=4,
                                 markevery=5,
                                 label='Current')
        
        capacity_line, = ax3.plot([], [], 
                                  color=self.colors[slot]["capacity"], 
                                  linewidth=2.5, 
                                  marker='^', 
                                  markersize=4,
                                  markevery=5,
                                  label='Simpson Capacity (mAh)')

        # Set labels and titles
        ax1.set_ylabel("Voltage (V)", fontweight='bold', fontsize=11)
        ax2.set_ylabel("Current (mA)", fontweight='bold', fontsize=11)
        ax3.set_ylabel("Capacity (mAh)", fontweight='bold', fontsize=11)
        ax3.set_xlabel("Time (seconds)", fontweight='bold', fontsize=11)

        ax1.set_title(f"Slot {slot} - Voltage vs Time", fontweight='bold', fontsize=12, pad=10)
        ax2.set_title(f"Slot {slot} - Current vs Time", fontweight='bold', fontsize=12, pad=10)
        ax3.set_title(f"Slot {slot} - Simpson Capacity vs Time", fontweight='bold', fontsize=12, pad=10)

        # Add legends
        ax1.legend(loc='best', frameon=True, facecolor='white', edgecolor='#cccccc')
        ax2.legend(loc='best', frameon=True, facecolor='white', edgecolor='#cccccc')
        ax3.legend(loc='best', frameon=True, facecolor='white', edgecolor='#cccccc')

        # Store everything
        self.graph_frames[slot] = {
            "canvas": canvas,
            "fig": fig,
            "ax1": ax1,
            "ax2": ax2,
            "ax3": ax3,
            "voltage_line": voltage_line,
            "current_line": current_line,
            "capacity_line": capacity_line
        }

    # ================= ZOOM CONTROL FUNCTIONS =================
    def zoom_out_all(self, slot):
        """Zoom out completely to show all data"""
        if slot not in self.graph_frames:
            return
        
        data = self.slots[slot]
        graphs = self.graph_frames[slot]
        
        if not data["time"] or len(data["time"]) == 0:
            return
        
        # Reset all zoom levels
        self.zoom_levels[slot] = {
            "x_range": None,
            "y1_range": None,
            "y2_range": None,
            "y3_range": None
        }
        
        # Reset all axes to auto-scale
        for ax in [graphs["ax1"], graphs["ax2"], graphs["ax3"]]:
            ax.relim()
            ax.autoscale_view()
        
        graphs["canvas"].draw_idle()
        self.log(f"Slot {slot}: View reset to show all data")

    def zoom_out_axis(self, slot, axis):
        """Zoom out on specific axis"""
        if slot not in self.graph_frames:
            return
        
        data = self.slots[slot]
        graphs = self.graph_frames[slot]
        
        if not data["time"] or len(data["time"]) == 0:
            return
        
        if axis == 'x':
            # Zoom out X axis by 20%
            for ax in [graphs["ax1"], graphs["ax2"], graphs["ax3"]]:
                xmin, xmax = ax.get_xlim()
                xrange = xmax - xmin
                new_xmin = xmin - xrange * 0.1
                new_xmax = xmax + xrange * 0.1
                ax.set_xlim(new_xmin, new_xmax)
            
            self.zoom_levels[slot]["x_range"] = (new_xmin, new_xmax)
            
        elif axis == 'y':
            # Zoom out Y axes by 20%
            ax_pairs = [(graphs["ax1"], "y1_range"), 
                       (graphs["ax2"], "y2_range"), 
                       (graphs["ax3"], "y3_range")]
            
            for ax, range_key in ax_pairs:
                ymin, ymax = ax.get_ylim()
                yrange = ymax - ymin
                new_ymin = ymin - yrange * 0.1
                new_ymax = ymax + yrange * 0.1
                ax.set_ylim(new_ymin, new_ymax)
                self.zoom_levels[slot][range_key] = (new_ymin, new_ymax)
        
        graphs["canvas"].draw_idle()
        self.log(f"Slot {slot}: Zoomed out {axis.upper()}-axis")

    def reset_view(self, slot):
        """Reset view to show all data"""
        self.zoom_out_all(slot)

    # ================= STATISTICS TAB =================
    def create_stats_tab(self):
        """Create statistics tab with all slots"""
        frame = ttk.Frame(self.notebook)
        self.notebook.add(frame, text="All Slots")

        # Add zoom controls for stats tab
        toolbar_frame = ttk.Frame(frame)
        toolbar_frame.pack(fill=tk.X, padx=5, pady=2)

        zoom_frame = ttk.LabelFrame(toolbar_frame, text="Zoom Controls", padding=5)
        zoom_frame.pack(side=tk.LEFT, padx=5)

        ttk.Button(zoom_frame, text="Zoom Out All Stats", 
                  command=self.zoom_out_stats).pack(side=tk.LEFT, padx=2)
        ttk.Button(zoom_frame, text="Reset Stats View", 
                  command=self.reset_stats_view).pack(side=tk.LEFT, padx=2)

        # Create figure with 2x2 grid
        fig = plt.figure(figsize=(9, 7))
        fig.patch.set_facecolor('#f0f0f0')

        # Create subplots
        ax1 = fig.add_subplot(2, 2, 1)
        ax2 = fig.add_subplot(2, 2, 2)
        ax3 = fig.add_subplot(2, 2, 3)
        ax4 = fig.add_subplot(2, 2, 4)

        # Style all axes
        colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728']
        markers = ['o', 's', '^', 'D']
        
        for ax in [ax1, ax2, ax3, ax4]:
            ax.set_facecolor('#ffffff')
            ax.grid(True, linestyle='--', alpha=0.7, color='#cccccc')
            ax.spines['top'].set_visible(False)
            ax.spines['right'].set_visible(False)

        # Set titles
        ax1.set_title("Voltage Over Time - All Slots", fontweight='bold', fontsize=11)
        ax2.set_title("Current Over Time - All Slots", fontweight='bold', fontsize=11)
        ax3.set_title("Simpson Capacity Over Time - All Slots", fontweight='bold', fontsize=11)
        ax4.set_title("Live Statistics", fontweight='bold', fontsize=11)

        # Set labels
        ax1.set_ylabel("Voltage (V)")
        ax2.set_ylabel("Current (mA)")
        ax3.set_ylabel("Capacity (mAh)")
        ax3.set_xlabel("Time (s)")
        ax4.axis('off')

        # Create lines for each slot
        voltage_lines = []
        current_lines = []
        capacity_lines = []

        for i, slot in enumerate("ABCD"):
            # Voltage line
            line, = ax1.plot([], [], color=colors[i], linewidth=2, 
                           marker=markers[i], markersize=3, markevery=10,
                           label=f"Slot {slot}")
            voltage_lines.append(line)
            
            # Current line
            line, = ax2.plot([], [], color=colors[i], linewidth=2, 
                           marker=markers[i], markersize=3, markevery=10,
                           label=f"Slot {slot}")
            current_lines.append(line)
            
            # Capacity line (Simpson)
            line, = ax3.plot([], [], color=colors[i], linewidth=2, 
                           marker=markers[i], markersize=3, markevery=10,
                           label=f"Slot {slot}")
            capacity_lines.append(line)

        # Add legends
        ax1.legend(loc='best', fontsize=8)
        ax2.legend(loc='best', fontsize=8)
        ax3.legend(loc='best', fontsize=8)

        # Create canvas
        canvas = FigureCanvasTkAgg(fig, master=frame)
        toolbar = NavigationToolbar2Tk(canvas, frame)
        toolbar.update()
        canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        # Store everything
        self.stats_display = {
            "canvas": canvas,
            "fig": fig,
            "ax1": ax1,
            "ax2": ax2,
            "ax3": ax3,
            "ax4": ax4,
            "voltage_lines": voltage_lines,
            "current_lines": current_lines,
            "capacity_lines": capacity_lines
        }

    def zoom_out_stats(self):
        """Zoom out all stats graphs"""
        if not hasattr(self, 'stats_display'):
            return
        
        # Zoom out each axis by 20%
        for ax_name in ["ax1", "ax2", "ax3"]:
            ax = self.stats_display[ax_name]
            
            # Zoom X axis
            xmin, xmax = ax.get_xlim()
            xrange = xmax - xmin
            ax.set_xlim(xmin - xrange * 0.1, xmax + xrange * 0.1)
            
            # Zoom Y axis
            ymin, ymax = ax.get_ylim()
            yrange = ymax - ymin
            ax.set_ylim(ymin - yrange * 0.1, ymax + yrange * 0.1)
        
        self.stats_display["canvas"].draw_idle()
        self.log("Statistics view zoomed out")

    def reset_stats_view(self):
        """Reset stats graphs to show all data"""
        if not hasattr(self, 'stats_display'):
            return
        
        for ax_name in ["ax1", "ax2", "ax3"]:
            ax = self.stats_display[ax_name]
            ax.relim()
            ax.autoscale_view()
        
        self.stats_display["canvas"].draw_idle()
        self.log("Statistics view reset")

    # ================= 3D TAB =================
    def create_3d_tab(self):
        """Create 3D visualization tab"""
        frame = ttk.Frame(self.notebook)
        self.notebook.add(frame, text="3D View")

        # Add zoom controls for 3D
        toolbar_frame = ttk.Frame(frame)
        toolbar_frame.pack(fill=tk.X, padx=5, pady=2)

        zoom_frame = ttk.LabelFrame(toolbar_frame, text="3D Controls", padding=5)
        zoom_frame.pack(side=tk.LEFT, padx=5)

        ttk.Button(zoom_frame, text="Reset 3D View", 
                  command=self.reset_3d_view).pack(side=tk.LEFT, padx=2)

        fig = plt.figure(figsize=(9, 7))
        fig.patch.set_facecolor('#f0f0f0')
        ax = fig.add_subplot(111, projection='3d')
        ax.set_facecolor('#ffffff')
        
        ax.xaxis.pane.fill = False
        ax.yaxis.pane.fill = False
        ax.zaxis.pane.fill = False
        ax.grid(True, linestyle='--', alpha=0.7)

        canvas = FigureCanvasTkAgg(fig, master=frame)
        canvas.get_tk_widget().pack(fill=tk.BOTH, expand=True)

        self.graph_3d = {"ax": ax, "canvas": canvas}

    def reset_3d_view(self):
        """Reset 3D view to default angles"""
        if not hasattr(self, 'graph_3d'):
            return
        
        ax = self.graph_3d["ax"]
        ax.view_init(elev=30, azim=-60)
        self.graph_3d["canvas"].draw_idle()
        self.log("3D view reset")

    # ================= WIFI CONNECTION =================
    def connect_wifi(self):
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.connect((self.esp32_ip.get(), self.esp32_port))
            self.socket.settimeout(0.1)
            self.connected = True
            threading.Thread(target=self.receive_data, daemon=True).start()
            self.log("Connected to ESP32")
            self.status_label.config(text="● Connected", foreground="green")
        except Exception as e:
            self.log(f"Connection Error: {e}")

    def disconnect_wifi(self):
        self.connected = False
        if self.socket:
            self.socket.close()
        self.log("Disconnected from ESP32")
        self.status_label.config(text="● Disconnected", foreground="red")

    def receive_data(self):
        buffer = ""
        while self.connected:
            try:
                data = self.socket.recv(1024).decode()
                buffer += data
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    try:
                        self.process_json(json.loads(line.strip()))
                    except json.JSONDecodeError:
                        pass
            except socket.timeout:
                pass
            except Exception as e:
                self.log(f"Receive error: {e}")
                break

    # ================= DATA PROCESSING =================
    def process_json(self, data):
        if not self.recording:
            return

        if data.get("type") == "sensor_data":
            s = data["slot"]
            t = time.time() - self.start_time

            # Get battery number from data
            battery_num = data.get("battery_num", 0)
            mode = data.get("mode", "IDLE")
            cycle_current = data.get("cycle_current", 0)
            cycle_target = data.get("cycle_target", 0)
            
            # Store battery number
            self.battery_numbers[s] = battery_num

            # Store data
            self.slots[s]["time"].append(t)
            self.slots[s]["voltage"].append(data["voltage"])
            self.slots[s]["current"].append(data["current"])
            self.slots[s]["mode"] = mode
            self.slots[s]["cycle_current"] = cycle_current
            self.slots[s]["cycle_target"] = cycle_target

            # Calculate Simpson's 1/3 rule capacity
            self.compute_simpson_capacity(s)
            
            # Queue for graph update
            self.data_queue.put(s)

            # Update live labels with battery number and mode
            batt_display = f"#{battery_num}" if battery_num > 0 else "No Batt#"
            cycle_display = f" Cyc:{cycle_current}/{cycle_target}" if cycle_target > 0 else ""
            
            self.live_labels[s].config(
                text=f"{batt_display} | {data['voltage']:.2f}V | {data['current']:.0f}mA | {self.slots[s]['simpson_capacity']:.2f}mAh | {mode}{cycle_display}"
            )

            # Update statistics table
            if hasattr(self, 'stats_display'):
                self.update_stats_table()

            # Log only if this slot is currently selected
            current_tab = self.notebook.index(self.notebook.select())
            
            # Slot tabs are indices 0,1,2,3 (A,B,C,D)
            if current_tab < 4:  # If a slot tab is selected
                selected_slot = "ABCD"[current_tab]
                if selected_slot == s:  # Only log if it matches the incoming data's slot
                    cycle_info = f" | Cyc:{cycle_current}/{cycle_target}" if cycle_target > 0 else ""
                    self.log(
                        f"Slot {s} | Batt#{battery_num} | Time: {t:.1f}s | "
                        f"V: {data['voltage']:.2f}V | "
                        f"I: {data['current']:.0f}mA | "
                        f"Simpson Cap: {self.slots[s]['simpson_capacity']:.2f}mAh | "
                        f"Mode: {mode}{cycle_info}"
                    )

    def compute_simpson_capacity(self, s):
        """Compute capacity using Simpson's 1/3 rule"""
        t = self.slots[s]["time"]
        i = self.slots[s]["current"]
        n = len(i)

        if n < 2:
            self.slots[s]["simpson_capacity"] = 0
            self.slots[s]["capacity"].append(0)
            return

        # Simpson's 1/3 rule
        if n >= 3:
            # Use Simpson's rule for odd number of points
            h = (t[-1] - t[0]) / (n - 1)
            total = i[0] + i[-1]
            
            for k in range(1, n-1):
                if k % 2 == 0:  # Even indices (2,4,6...)
                    total += 2 * i[k]
                else:  # Odd indices (1,3,5...)
                    total += 4 * i[k]
            
            integral = (h / 3) * total
        else:
            # For 2 points, use trapezoidal rule
            integral = (i[-2] + i[-1]) * (t[-1] - t[-2]) / 2

        # Convert from mA*s to mAh (divide by 3600)
        capacity_mah = integral / 3600.0
        self.slots[s]["simpson_capacity"] = capacity_mah
        self.slots[s]["capacity"].append(capacity_mah)

    # ================= GRAPH UPDATES =================
    def update_display(self):
        """Main update loop"""
        try:
            while True:
                s = self.data_queue.get_nowait()
                self.update_slot_graph(s)
        except queue.Empty:
            pass

        self.update_stats_graphs()
        self.update_3d_graph()
        self.root.after(self.animation_speed, self.update_display)

    def update_slot_graph(self, slot):
        """Update individual slot graph"""
        if slot not in self.graph_frames:
            return

        data = self.slots[slot]
        graphs = self.graph_frames[slot]

        if not data["time"] or len(data["time"]) == 0:
            return

        # Update line data
        graphs["voltage_line"].set_data(data["time"], data["voltage"])
        graphs["current_line"].set_data(data["time"], data["current"])
        graphs["capacity_line"].set_data(data["time"], data["capacity"])

        # Only auto-scale if not zoomed
        if self.zoom_levels[slot]["x_range"] is None:
            # Auto-scale each axis
            for ax, values in [(graphs["ax1"], data["voltage"]), 
                              (graphs["ax2"], data["current"]), 
                              (graphs["ax3"], data["capacity"])]:
                if values:
                    ax.relim()
                    ax.autoscale_view()
                    
                    # Add small padding
                    ymin, ymax = ax.get_ylim()
                    if ymin != ymax:
                        padding = (ymax - ymin) * 0.05
                        ax.set_ylim(ymin - padding, ymax + padding)

            # Update x-axis limit
            if data["time"]:
                xmin, xmax = min(data["time"]), max(data["time"])
                if xmin != xmax:
                    padding = (xmax - xmin) * 0.05
                    graphs["ax1"].set_xlim(xmin - padding, xmax + padding)
                    graphs["ax2"].set_xlim(xmin - padding, xmax + padding)
                    graphs["ax3"].set_xlim(xmin - padding, xmax + padding)
        else:
            # Restore zoomed ranges
            if self.zoom_levels[slot]["x_range"]:
                xmin, xmax = self.zoom_levels[slot]["x_range"]
                graphs["ax1"].set_xlim(xmin, xmax)
                graphs["ax2"].set_xlim(xmin, xmax)
                graphs["ax3"].set_xlim(xmin, xmax)
            
            if self.zoom_levels[slot]["y1_range"]:
                graphs["ax1"].set_ylim(self.zoom_levels[slot]["y1_range"])
            if self.zoom_levels[slot]["y2_range"]:
                graphs["ax2"].set_ylim(self.zoom_levels[slot]["y2_range"])
            if self.zoom_levels[slot]["y3_range"]:
                graphs["ax3"].set_ylim(self.zoom_levels[slot]["y3_range"])

        # Force redraw
        graphs["canvas"].draw_idle()

    def update_stats_graphs(self):
        """Update statistics tab graphs"""
        if not hasattr(self, 'stats_display'):
            return

        # Update each line
        for i, slot in enumerate("ABCD"):
            data = self.slots[slot]
            
            if data["time"] and len(data["time"]) > 0:
                # Update voltage lines
                if i < len(self.stats_display["voltage_lines"]):
                    self.stats_display["voltage_lines"][i].set_data(data["time"], data["voltage"])
                
                # Update current lines
                if i < len(self.stats_display["current_lines"]):
                    self.stats_display["current_lines"][i].set_data(data["time"], data["current"])
                
                # Update capacity lines (Simpson)
                if i < len(self.stats_display["capacity_lines"]):
                    self.stats_display["capacity_lines"][i].set_data(data["time"], data["capacity"])

        # Auto-scale all axes
        for ax_name in ["ax1", "ax2", "ax3"]:
            ax = self.stats_display[ax_name]
            ax.relim()
            ax.autoscale_view()

        # Update statistics table
        self.update_stats_table()

        # Redraw
        self.stats_display["canvas"].draw_idle()

    def update_stats_table(self):
        """Update the statistics table with battery numbers"""
        ax4 = self.stats_display["ax4"]
        ax4.clear()
        ax4.axis('off')

        # Prepare table data with Simpson capacity and battery numbers
        table_data = []
        for slot in "ABCD":
            if self.slots[slot]["time"] and len(self.slots[slot]["time"]) > 0:
                latest_v = self.slots[slot]["voltage"][-1]
                latest_i = self.slots[slot]["current"][-1]
                latest_cap = self.slots[slot]["simpson_capacity"]
                data_points = len(self.slots[slot]["time"])
                mode = self.slots[slot]["mode"]
                cycle_info = f"{self.slots[slot]['cycle_current']}/{self.slots[slot]['cycle_target']}" if self.slots[slot]['cycle_target'] > 0 else "-"
                
                # Calculate min/max
                min_v = min(self.slots[slot]["voltage"])
                max_v = max(self.slots[slot]["voltage"])
                min_i = min(self.slots[slot]["current"])
                max_i = max(self.slots[slot]["current"])
                
                # Get battery number
                batt_num = self.battery_numbers[slot] if self.battery_numbers[slot] > 0 else "---"
                
                row = [
                    f"Slot {slot}",
                    f"{latest_v:.2f}V",
                    f"{latest_i:.0f}mA",
                    f"{latest_cap:.2f}mAh",
                    f"{mode}",
                    f"{cycle_info}",
                    f"{batt_num}",
                    f"{data_points} pts"
                ]
            else:
                row = [f"Slot {slot}", "---", "---", "---", "---", "---", "---", "0 pts"]
            table_data.append(row)

        # Create table with 8 columns
        table = ax4.table(cellText=table_data,
                         colLabels=["Slot", "V", "I", "Cap(mAh)", "Mode", "Cycle", "Batt#", "Samples"],
                         cellLoc='center',
                         loc='center',
                         colColours=['#4472c4'] * 8)

        table.auto_set_font_size(False)
        table.set_fontsize(6)  # Smaller font for 8 columns
        table.scale(1.2, 1.8)

        # Style the table
        for (row, col), cell in table.get_celld().items():
            if row == 0:
                cell.set_text_props(weight='bold', color='white')
                cell.set_facecolor('#4472c4')
            else:
                if col == 0:
                    cell.set_facecolor('#e6f0ff')
                elif row % 2 == 0:
                    cell.set_facecolor('#f9f9f9')
                else:
                    cell.set_facecolor('#ffffff')

        ax4.set_title("Live Data Summary with Simpson Capacity", fontweight='bold', fontsize=10, pad=10)

    def update_3d_graph(self):
        """Update 3D visualization"""
        ax = self.graph_3d["ax"]
        ax.clear()

        colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728']
        markers = ['o', 's', '^', 'D']
        
        for i, s in enumerate("ABCD"):
            t = self.slots[s]["time"]
            c = self.slots[s]["current"]
            v = self.slots[s]["voltage"]

            if t and len(t) > 0:
                # Plot every 5th point for performance
                step = max(1, len(t) // 50)
                ax.scatter(t[::step], c[::step], v[::step], 
                          color=colors[i], marker=markers[i], s=20,
                          label=f"Slot {s} (Batt#{self.battery_numbers[s]})")

        ax.set_xlabel("Time (s)")
        ax.set_ylabel("Current (mA)")
        ax.set_zlabel("Voltage (V)")
        ax.set_title("3D Battery Performance")
        ax.legend()

        self.graph_3d["canvas"].draw_idle()

    # ================= UTILITY FUNCTIONS =================
    def clear_all_data(self):
        """Clear all stored data"""
        for s in "ABCD":
            self.slots[s] = {
                "time": [],
                "voltage": [],
                "current": [],
                "simpson_capacity": 0,
                "capacity": [],
                "mode": "IDLE",
                "cycle_current": 0,
                "cycle_target": 0
            }
            self.battery_numbers[s] = 0
            # Reset zoom levels
            self.zoom_levels[s] = {
                "x_range": None,
                "y1_range": None,
                "y2_range": None,
                "y3_range": None
            }
        
        self.start_time = time.time()
        
        # Clear all graphs
        for slot in "ABCD":
            if slot in self.graph_frames:
                graphs = self.graph_frames[slot]
                graphs["voltage_line"].set_data([], [])
                graphs["current_line"].set_data([], [])
                graphs["capacity_line"].set_data([], [])
                
                for ax in [graphs["ax1"], graphs["ax2"], graphs["ax3"]]:
                    ax.relim()
                    ax.autoscale_view()
                
                graphs["canvas"].draw_idle()
        
        # Clear stats graphs
        if hasattr(self, 'stats_display'):
            for lines in [self.stats_display["voltage_lines"], 
                         self.stats_display["current_lines"], 
                         self.stats_display["capacity_lines"]]:
                for line in lines:
                    line.set_data([], [])
            
            for ax_name in ["ax1", "ax2", "ax3"]:
                self.stats_display[ax_name].relim()
                self.stats_display[ax_name].autoscale_view()
            
            self.stats_display["canvas"].draw_idle()
        
        # Reset labels
        for s in "ABCD":
            self.live_labels[s].config(text="No Batt# | --- V | --- mA | --- Ah | ---")
        
        self.log("All data cleared")

    def load_history(self):
        """Load data from CSV file"""
        file = filedialog.askopenfilename(filetypes=[("CSV files", "*.csv")])
        if not file:
            return

        try:
            with open(file, newline='') as f:
                reader = csv.DictReader(f)
                for row in reader:
                    s = row["Slot"]
                    self.slots[s]["time"].append(float(row["Time"]))
                    self.slots[s]["voltage"].append(float(row["Voltage"]))
                    self.slots[s]["current"].append(float(row["Current"]))
                    # Try to load battery number if available
                    if "Battery_Num" in row:
                        self.battery_numbers[s] = int(row["Battery_Num"])
                    # Recompute Simpson capacity for all data
                    self.compute_simpson_capacity(s)
            self.log(f"History loaded from {file}")
        except Exception as e:
            self.log(f"Error loading history: {e}")

    def export_all(self):
        """Export all data to CSV including Simpson capacity and battery numbers"""
        file = filedialog.asksaveasfilename(defaultextension=".csv")
        if not file:
            return

        try:
            with open(file, "w", newline='') as f:
                writer = csv.writer(f)
                writer.writerow(["Slot", "Battery_Num", "Time", "Voltage", "Current", "Simpson_Capacity_Ah", "Mode", "Cycle_Current", "Cycle_Target"])

                for s in "ABCD":
                    for i in range(len(self.slots[s]["time"])):
                        writer.writerow([
                            s,
                            self.battery_numbers[s],
                            self.slots[s]["time"][i],
                            self.slots[s]["voltage"][i],
                            self.slots[s]["current"][i],
                            self.slots[s]["capacity"][i] if i < len(self.slots[s]["capacity"]) else 0,
                            self.slots[s]["mode"],
                            self.slots[s]["cycle_current"],
                            self.slots[s]["cycle_target"]
                        ])
            self.log(f"Data exported to {file}")
        except Exception as e:
            self.log(f"Error exporting: {e}")

    def log(self, msg):
        """Add message to log"""
        t = datetime.now().strftime("%H:%M:%S")
        self.log_text.insert(tk.END, f"[{t}] {msg}\n")
        self.log_text.see(tk.END)


def main():
    root = tk.Tk()
    app = WiFiBatteryMonitor(root)
    root.mainloop()


if __name__ == "__main__":
    main()