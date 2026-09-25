from flask import Flask, request, jsonify, render_template

app = Flask(__name__)

# Latest data received from Node 2
latest_data = {
    "node1": {},
    "node2": {},
    "systemStatus": 0,
    "node1Connected": False
}


@app.route("/")
def dashboard():
    return render_template("index.html")


@app.route("/api/data", methods=["POST"])
def receive_data():

    global latest_data

    data = request.get_json()

    if data is None:
        return jsonify({
            "success": False,
            "message": "No JSON data received"
        }), 400

    latest_data = data

    print("\n========== DATA RECEIVED ==========")
    print(data)
    print("===================================\n")

    return jsonify({
        "success": True,
        "message": "Data received"
    })


@app.route("/api/data", methods=["GET"])
def get_data():

    return jsonify(latest_data)


if __name__ == "__main__":

    print("====================================")
    print("          MINE GUARD")
    print(" Smart Mine Subsidence Monitoring")
    print("====================================")
    print("Server starting...")
    print("Dashboard: http://YOUR_PC_IP:5000")
    print("API:       http://YOUR_PC_IP:5000/api/data")
    print("====================================")

    app.run(
        host="0.0.0.0",
        port=5000,
        debug=False
    )
