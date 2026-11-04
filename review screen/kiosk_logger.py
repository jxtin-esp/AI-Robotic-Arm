
import csv
import time
from datetime import datetime
from pathlib import Path

import serial
from serial.tools import list_ports

# CHANGE THIS to your ESP32-S3 COM port.
COM_PORT = "COM5"
BAUD_RATE = 115200

OUTPUT_FILE = Path(__file__).with_name("kiosk_reviews.csv")


def show_ports():
    ports = list(list_ports.comports())

    if not ports:
        print("No serial ports detected.")
        return

    for port in ports:
        print(f"{port.device}: {port.description}")


def main():
    print("PRODUCT REVIEW KIOSK LOGGER")
    print("Detected serial ports:")
    show_ports()

    print(f"\nConnecting to {COM_PORT}...")

    try:
        with serial.Serial(COM_PORT, BAUD_RATE, timeout=1) as ser:
            time.sleep(2)
            ser.reset_input_buffer()

            new_file = (
                not OUTPUT_FILE.exists()
                or OUTPUT_FILE.stat().st_size == 0
            )

            with OUTPUT_FILE.open(
                "a", newline="", encoding="utf-8"
            ) as csv_file:

                writer = csv.writer(csv_file)

                if new_file:
                    writer.writerow([
                        "timestamp",
                        "rating_stars",
                        "total_ratings",
                        "average_rating"
                    ])
                    csv_file.flush()

                print("\nConnected! Waiting for reviews...")
                print("Press Ctrl+C to stop.\n")

                while True:
                    raw = ser.readline().decode(
                        "utf-8", errors="replace"
                    ).strip()

                    if not raw:
                        continue

                    if raw == "KIOSK_READY":
                        print("Kiosk ready.")
                        continue

                    if not raw.startswith("REVIEW,"):
                        continue

                    parts = raw.split(",")

                    if len(parts) != 4:
                        print("Invalid message:", raw)
                        continue

                    try:
                        stars = int(parts[1])
                        total = int(parts[2])
                        average = float(parts[3])

                        if stars < 1 or stars > 5:
                            raise ValueError()

                    except ValueError:
                        print("Invalid review:", raw)
                        continue

                    timestamp = datetime.now().astimezone().isoformat(
                        timespec="seconds"
                    )

                    writer.writerow([
                        timestamp,
                        stars,
                        total,
                        f"{average:.2f}"
                    ])

                    csv_file.flush()

                    print(
                        f"Saved {stars}/5 stars | "
                        f"Total: {total} | "
                        f"Average: {average:.2f}"
                    )

    except serial.SerialException as error:
        print(f"\nCould not connect to {COM_PORT}: {error}")
        print("Check the COM port and close Arduino Serial Monitor.")

    except KeyboardInterrupt:
        print("\nLogger stopped. CSV saved.")


if __name__ == "__main__":
    main()
