from flask import Flask, render_template, jsonify, request
import time
import sqlite3

app = Flask(__name__)

DATABASE = "strataguard.db"

latest_data = {
    "timestamp": 0,
    "node1": {
        "active": False,
        "tilt_x": 0,
        "tilt_y": 0,
        "vibration": 0,
        "crack": 0,
        "displacement": 0,
        "overallStatus": 0
    },
    "node2": {
        "active": False,
        "tilt_x": 0,
        "tilt_y": 0,
        "vibration": 0,
        "crack": 0,
        "displacement": 0,
        "overallStatus": 0
    },
    "distance": 0
}


def db():
    connection = sqlite3.connect(DATABASE)
    connection.row_factory = sqlite3.Row
    return connection


def init_database():
    connection = db()
    connection.execute("""
        CREATE TABLE IF NOT EXISTS sensor_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp REAL NOT NULL,
            node_id INTEGER NOT NULL,
            tilt_x REAL DEFAULT 0,
            tilt_y REAL DEFAULT 0,
            vibration REAL DEFAULT 0,
            displacement REAL DEFAULT 0,
            crack REAL DEFAULT 0,
            overall_status INTEGER DEFAULT 0
        )
    """)
    connection.commit()
    connection.close()


def save_sensor_data(data):
    connection = db()
    timestamp = data.get("timestamp", time.time())

    for node_id, key in ((1, "node1"), (2, "node2")):
        node = data.get(key, {}) or {}
        connection.execute("""
            INSERT INTO sensor_history
            (
                timestamp, node_id, tilt_x, tilt_y,
                vibration, displacement, crack, overall_status
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
        """, (
            timestamp,
            node_id,
            node.get("tilt_x", node.get("tiltX", 0)),
            node.get("tilt_y", node.get("tiltY", 0)),
            node.get("vibration", 0),
            node.get("displacement", 0),
            node.get("crack", 0),
            node.get("overallStatus", node.get("overall_status", 0))
        ))

    connection.commit()
    connection.close()


@app.get("/")
def dashboard():
    return render_template("index.html")


@app.route("/api/data", methods=["GET", "POST"])
def api_data():
    global latest_data

    if request.method == "POST":
        data = request.get_json(silent=True)

        if not isinstance(data, dict):
            return jsonify({
                "success": False,
                "error": "Invalid JSON"
            }), 400

        data["timestamp"] = time.time()
        latest_data = data

        try:
            save_sensor_data(data)
        except Exception as error:
            print("Database error:", error)

        response = jsonify({
            "success": True,
            "message": "Data received"
        })

        response.headers["Connection"] = "close"
        return response, 200

    return jsonify(latest_data), 200


@app.get("/api/history")
def history():
    limit = request.args.get("limit", default=500, type=int)
    limit = max(1, min(limit, 1000))

    connection = db()
    rows = connection.execute("""
        SELECT
            id, timestamp, node_id, tilt_x, tilt_y,
            vibration, displacement, crack, overall_status
        FROM sensor_history
        ORDER BY timestamp DESC, id DESC
        LIMIT ?
    """, (limit,)).fetchall()
    connection.close()

    return jsonify([
        {
            "id": row["id"],
            "timestamp": row["timestamp"],
            "node_id": row["node_id"],
            "tilt_x": row["tilt_x"],
            "tilt_y": row["tilt_y"],
            "vibration": row["vibration"],
            "displacement": row["displacement"],
            "crack": row["crack"],
            "overall_status": row["overall_status"]
        }
        for row in rows
    ])


if __name__ == "__main__":
    init_database()

    print("========================================")
    print("       STRATAGUARD SERVER")
    print("========================================")
    print("Dashboard: http://127.0.0.1:5000")
    print("API:       http://127.0.0.1:5000/api/data")
    print("History:   http://127.0.0.1:5000/api/history")
    print("Database:  strataguard.db")
    print("========================================")

    app.run(
        host="0.0.0.0",
        port=5000,
        debug=False,
        threaded=True,
        use_reloader=False
    )
