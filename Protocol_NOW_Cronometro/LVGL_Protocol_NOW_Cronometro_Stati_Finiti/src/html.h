# pragma once 

#include<Arduino.h>
// === WebServer handlers ===
  String page = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset="UTF-8">
      <title>Cronometro ESP32</title>
      <style>
        body { font-family: Arial; text-align: center; background: #111; color: #eee; }
        .time { font-size: 48px; margin: 20px; }
        button { font-size: 20px; padding: 10px 20px; margin: 10px; border-radius: 10px; }
      </style>
    </head>
    <body>
      <h1>Cronometro ESP32</h1>
      <div class="time" id="chrono">00:00.0</div>
      <button onclick="sendCmd('start')">Avvia</button>
      <button onclick="sendCmd('stop')">Ferma</button>
      <button onclick="sendCmd('reset')">Reset</button>

      <script>
        function updateTime() {
          fetch('/time').then(r => r.text()).then(t => {
            document.getElementById('chrono').innerText = t;
          });
        }
        setInterval(updateTime, 200);

        function sendCmd(cmd) {
          fetch('/' + cmd).then(updateTime);
        }
      </script>
    </body>
    </html>
  )rawliteral";
