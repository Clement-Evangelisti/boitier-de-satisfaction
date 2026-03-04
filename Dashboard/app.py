from flask import Flask, render_template, jsonify
import json

JSON_FILE  = r"Dashboard\donnees.json"
app = Flask(__name__)

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/data")
def data():
    with open(JSON_FILE) as f:
        return jsonify(json.load(f))

if __name__ == "__main__":
    app.run(debug=True)