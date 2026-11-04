
import csv
import os
import serial
import time
from datetime import datetime

# CHANGE THIS to your ESP32 COM port
PORT = "COM5"
BAUD = 115200

OUTPUT_FILE = "kiosk_reviews.csv"

file_exists = os.path.exists(OUTPUT_FILE)

with serial.Serial(PORT, BAUD, timeout=1) as esp:
    print(f"Connected to kiosk on {PORT}")
    time.sleep(2)

    with open(OUTPUT_FILE, "a", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)

        if not file_exists or os.path.getsize(OUTPUT_FILE) == 0:
            writer.writerow([
                "Timestamp",
                "Rating",
                "Total Ratings",
                "Average Rating"
            ])

        print(f"Saving reviews to {OUTPUT_FILE}")
        print("Waiting for ratings... Press Ctrl+C to stop.")

        try:
            while True:
                line = esp.readline().decode(
                    "utf-8", errors="ignore"
                ).strip()

                if not line.startswith("REVIEW,"):
                    continue

                parts = line.split(",")

                if len(parts) != 4:
                    continue

                _, rating, total, average = parts

                timestamp = datetime.now().strftime(
                    "%Y-%m-%d %H:%M:%S"
                )

                writer.writerow([
                    timestamp,
                    rating,
                    total,
                    average
                ])

                file.flush()

                print(
                    f"[{timestamp}] "
                    f"{rating} stars | "
                    f"Total: {total} | "
                    f"Average: {average}"
                )

        except KeyboardInterrupt:
            print("\nLogger stopped.")
