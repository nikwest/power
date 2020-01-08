

const Websocket = require('ws');

//const addr = 'wss://mdash.net/api/v2/notify?access_token=3IYYiafTgcAk0SaQPPSpIw';
const addr = 'wss:'
const ws = new Websocket(addr, { origin: addr });
ws.on('message', function incoming(data) {
  console.log(data);
});
