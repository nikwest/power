

const Websocket = require('ws');

const addr = 'wss://mdash.net/api/v2/devices?access_token=3IYYiafTgcAk0SaQPPSpIw';
const ws = new Websocket(addr, { origin: addr });
ws.on('message', msg => console.log('Got message:', msg.toString()));

