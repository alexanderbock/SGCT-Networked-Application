'use strict'

const express = require('express');
const app = express();
const server = require('http').Server(app);
const WebSocketServer = require('websocket').server;


//
const port = 80;
const gameAddress = '::ffff:127.0.0.1';

app.get('/', function (req, res) {
  res.sendFile(__dirname + '/public/index.html');
});

server.listen(port, function () {
  console.log(`Server is listening on: ${server.address().address}:${port}`);
})

var gameSocket = null;
var wsServer = new WebSocketServer({ httpServer: server });
wsServer.on('request', function (req) {
  if (req.remoteAddress === gameAddress) {
    console.log('Game connection');

    gameSocket = req.accept("example-protocol", req.origin);

    gameSocket.on('frame', function(frame) {
      console.log(frame);
    });

    gameSocket.on('message', function(msg) {
      if (msg.type === 'utf8') {
        console.log(msg.utf8Data);
      }
      console.log(msg);
    });

    gameSocket.on('close', function(reason, desc) {
      console.log(`Game connection lost. Reason: ${reason}.  Description: ${desc}`);
      gameSocket = null;
    });
  }
  else {
    console.log(`Other connection from ${req.remoteAddress}`);
    request.accept(null, req.origin);
    gameSocket.send(req.remoteAddress);
  }
});
