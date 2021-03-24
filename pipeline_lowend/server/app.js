const path = require("path");
const express = require("express");
const WebSocket = require("ws");
const fs = require("fs");
const app = express();

const WS_PORT = 8888;
const HTTP_PORT = 8000;

const wsServer = new WebSocket.Server({ port: WS_PORT }, () =>
  console.log(`WS Server is listening at ${WS_PORT}`)
);

let connectedClients = [];
wsServer.on("connection", (ws, req) => {
  console.log("Connected");
  connectedClients.push(ws);

  ws.on("message", (data) => {
    connectedClients.forEach((ws, i) => {
      if (ws.readyState === ws.OPEN) {
        ws.send(data);
        const buf = Buffer.from(data);
        const date = new Date();
        const imagePath = "saved_images/image_" + date.toISOString() + ".jpg";
        // fs.writeFile(imagePath, buf, "binary",(err) => {
        //   if (!err) {
        //     console.log("Image written sucessfully to " + imagePath)
        //   } else {
        //     console.log("Error when writing image: " + err)
        //   }
        // });
      } else {
        connectedClients.splice(i, 1);
      }
    });
  });
});

app.get("/", (req, res) =>
  res.sendFile(path.resolve(__dirname, "./index.html"))
);
app.listen(HTTP_PORT, () =>
  console.log(`HTTP server listening at ${HTTP_PORT}`)
);
