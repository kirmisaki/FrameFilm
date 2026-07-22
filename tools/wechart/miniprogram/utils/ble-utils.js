// BLE 协议常量
const BLE_SERVICE_UUID = '00002000-0000-1000-8000-00805f9b34fb';
const BLE_CHARACTERISTIC_UUID = '00002001-0000-1000-8000-00805f9b34fb';
const BLE_CMD_HEAD = 0x55;

const BLE_FILM_TRANS_CH_FILE_START = 0x03;
const BLE_FILM_TRANS_CH_FILE_NAME = 0x00;
const BLE_FILM_TRANS_CH_FILE_LEN = 0x01;
const BLE_FILM_TRANS_CH_FILE_DATA = 0x02;
const BLE_FILM_TRANS_CH_FILE_STOP = 0x04;
const BLE_FILM_TRANS_CH_FILE_DELETE = 0x05;
const BLE_FILM_TRANS_CH_FILE_LIST = 0x06;
const BLE_FILM_TRANS_CH_FILE_DISPLAY = 0x07;
const BLE_FILM_TRANS_CH_FILE_DISPLAY_GET = 0x08;

const BLE_FILM_TRANS_CH_OTA_LEN = 0x10;
const BLE_FILM_TRANS_CH_OTA_DATA = 0x11;
const BLE_FILM_TRANS_CH_OTA_START = 0x12;
const BLE_FILM_TRANS_CH_OTA_STOP = 0x13;

const BLE_FILM_TRANS_CH_CTRL_MODE = 0x20;
const BLE_FILM_TRANS_CH_CTRL_MODE_GET = 0x21;
const BLE_FILM_TRANS_CH_CTRL_RESET = 0x22;
const BLE_FILM_TRANS_CH_CTRL_PWRREAD = 0x23;
const BLE_FILM_TRANS_CH_CTRL_REBOOT = 0x24;
const BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF = 0x25;
const BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET = 0x26;
const BLE_FILM_TRANS_CH_CTRL_SLEEPMODE = 0x27;
const BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET = 0x28;
const BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME = 0x29;
const BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET = 0x2A;
const BLE_FILM_TRANS_CH_CTRL_SDRESET = 0x2B;

const BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE = 0x30;
const BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE_GET = 0x31;
const BLE_FILM_TRANS_CH_CTRL_WIFI_SSID = 0x32;
const BLE_FILM_TRANS_CH_CTRL_WIFI_SSID_GET = 0x33;
const BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD = 0x34;
const BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD_GET = 0x35;
const BLE_FILM_TRANS_CH_CTRL_FILM_API_URL = 0x36;
const BLE_FILM_TRANS_CH_CTRL_FILM_API_URL_GET = 0x37;
const BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT = 0x38;
const BLE_FILM_TRANS_CH_CTRL_WIFI_DISCONNECT = 0x39;
const BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET = 0x3A;
const BLE_FILM_TRANS_CH_CTRL_WIFI_CLEAR = 0x3B;
const BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD = 0x3C;
const BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE = 0x3D;

const BLE_CHUNK_SIZE = 192;
const BLE_CTRL_DELAY = 50;
const BLE_DATA_DELAY = 2;

function calculateChecksum(data, len) {
  let sum = 0;
  for (let i = 0; i < len; i++) sum += data[i];
  return sum & 0xFF;
}

function buildCmdPacket(cmdType, data) {
  if (data === null || data === undefined) {
    const packet = new Uint8Array(4);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = cmdType;
    packet[2] = 0;
    packet[3] = calculateChecksum(packet, 3);
    return packet;
  }
  if (typeof data === 'number') {
    const packet = new Uint8Array(5);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = cmdType;
    packet[2] = 1;
    packet[3] = data;
    packet[4] = calculateChecksum(packet, 4);
    return packet;
  }
  if (data instanceof Uint8Array) {
    const packet = new Uint8Array(4 + data.length);
    packet[0] = BLE_CMD_HEAD;
    packet[1] = cmdType;
    packet[2] = data.length;
    packet.set(data, 3);
    packet[packet.length - 1] = calculateChecksum(packet, packet.length - 1);
    return packet;
  }
  return null;
}

function buildFileStartPacket() {
  return buildCmdPacket(BLE_FILM_TRANS_CH_FILE_START, null);
}

function buildFileNamePacket(fileName) {
  const nameBytes = new Uint8Array(fileName.length);
  for (let i = 0; i < fileName.length; i++) {
    nameBytes[i] = fileName.charCodeAt(i);
  }
  const packet = new Uint8Array(4 + nameBytes.length);
  packet[0] = BLE_CMD_HEAD;
  packet[1] = BLE_FILM_TRANS_CH_FILE_NAME;
  packet[2] = nameBytes.length;
  packet.set(nameBytes, 3);
  packet[packet.length - 1] = calculateChecksum(packet, packet.length - 1);
  return packet;
}

function buildFileLenPacket(fileSize) {
  const packet = new Uint8Array(8);
  packet[0] = BLE_CMD_HEAD;
  packet[1] = BLE_FILM_TRANS_CH_FILE_LEN;
  packet[2] = 4;
  packet[3] = (fileSize >> 24) & 0xFF;
  packet[4] = (fileSize >> 16) & 0xFF;
  packet[5] = (fileSize >> 8) & 0xFF;
  packet[6] = fileSize & 0xFF;
  packet[7] = calculateChecksum(packet, 7);
  return packet;
}

function buildFileDataPacket(chunk) {
  const packet = new Uint8Array(4 + chunk.length);
  packet[0] = BLE_CMD_HEAD;
  packet[1] = BLE_FILM_TRANS_CH_FILE_DATA;
  packet[2] = chunk.length;
  packet.set(chunk, 3);
  packet[packet.length - 1] = calculateChecksum(packet, packet.length - 1);
  return packet;
}

function buildFileStopPacket() {
  return buildCmdPacket(BLE_FILM_TRANS_CH_FILE_STOP, null);
}

function buildSleepTimePacket(timeMinutes) {
  const packet = new Uint8Array(6);
  packet[0] = BLE_CMD_HEAD;
  packet[1] = BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME;
  packet[2] = 2;
  packet[3] = (timeMinutes >> 8) & 0xFF;
  packet[4] = timeMinutes & 0xFF;
  packet[5] = calculateChecksum(packet, 5);
  return packet;
}

function buildStringPacket(cmdType, str, maxLen) {
  var bytes = [];
  for (var i = 0; i < str.length; i++) {
    var code = str.charCodeAt(i);
    if (code <= 0x7F) {
      bytes.push(code);
    } else if (code <= 0x7FF) {
      bytes.push(0xC0 | (code >> 6));
      bytes.push(0x80 | (code & 0x3F));
    } else {
      bytes.push(0xE0 | (code >> 12));
      bytes.push(0x80 | ((code >> 6) & 0x3F));
      bytes.push(0x80 | (code & 0x3F));
    }
  }
  var dataLen = Math.min(bytes.length, (maxLen || 64) - 1);
  var packet = new Uint8Array(4 + dataLen);
  packet[0] = BLE_CMD_HEAD;
  packet[1] = cmdType;
  packet[2] = dataLen;
  for (var j = 0; j < dataLen; j++) {
    packet[3 + j] = bytes[j];
  }
  packet[packet.length - 1] = calculateChecksum(packet, packet.length - 1);
  return packet;
}

function formatDuration(minutes) {
  if (minutes < 60) return minutes + '分钟';
  if (minutes === 60) return '1小时';
  const hours = Math.floor(minutes / 60);
  const mins = minutes % 60;
  if (mins === 0) return hours + '小时';
  return hours + '小时' + mins + '分钟';
}

function parseBleResponse(value) {
  if (!value || value.byteLength < 4) return null;
  const data = new Uint8Array(value.buffer || value);
  if (data[0] !== BLE_CMD_HEAD) return null;
  return {
    cmdType: data[1],
    cmdLen: data[2],
    data: data
  };
}

module.exports = {
  BLE_SERVICE_UUID, BLE_CHARACTERISTIC_UUID, BLE_CMD_HEAD,
  BLE_FILM_TRANS_CH_FILE_START, BLE_FILM_TRANS_CH_FILE_NAME,
  BLE_FILM_TRANS_CH_FILE_LEN, BLE_FILM_TRANS_CH_FILE_DATA,
  BLE_FILM_TRANS_CH_FILE_STOP, BLE_FILM_TRANS_CH_FILE_DELETE,
  BLE_FILM_TRANS_CH_FILE_LIST, BLE_FILM_TRANS_CH_FILE_DISPLAY,
  BLE_FILM_TRANS_CH_FILE_DISPLAY_GET,
  BLE_FILM_TRANS_CH_OTA_LEN, BLE_FILM_TRANS_CH_OTA_DATA,
  BLE_FILM_TRANS_CH_OTA_START, BLE_FILM_TRANS_CH_OTA_STOP,
  BLE_FILM_TRANS_CH_CTRL_MODE, BLE_FILM_TRANS_CH_CTRL_MODE_GET,
  BLE_FILM_TRANS_CH_CTRL_RESET, BLE_FILM_TRANS_CH_CTRL_PWRREAD,
  BLE_FILM_TRANS_CH_CTRL_REBOOT,
  BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF, BLE_FILM_TRANS_CH_CTRL_SLEEPONOFF_GET,
  BLE_FILM_TRANS_CH_CTRL_SLEEPMODE, BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_GET,
  BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME, BLE_FILM_TRANS_CH_CTRL_SLEEPMODE_TIME_GET,
  BLE_FILM_TRANS_CH_CTRL_SDRESET,
  BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE, BLE_FILM_TRANS_CH_CTRL_WIFI_ENABLE_GET,
  BLE_FILM_TRANS_CH_CTRL_WIFI_SSID, BLE_FILM_TRANS_CH_CTRL_WIFI_SSID_GET,
  BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD, BLE_FILM_TRANS_CH_CTRL_WIFI_PASSWORD_GET,
  BLE_FILM_TRANS_CH_CTRL_FILM_API_URL, BLE_FILM_TRANS_CH_CTRL_FILM_API_URL_GET,
  BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT, BLE_FILM_TRANS_CH_CTRL_WIFI_DISCONNECT,
  BLE_FILM_TRANS_CH_CTRL_WIFI_CONNECT_GET, BLE_FILM_TRANS_CH_CTRL_WIFI_CLEAR,
  BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD, BLE_FILM_TRANS_CH_CTRL_FILM_DOWNLOAD_STATE,
  BLE_CHUNK_SIZE, BLE_CTRL_DELAY, BLE_DATA_DELAY,
  calculateChecksum,
  buildCmdPacket, buildFileStartPacket, buildFileNamePacket,
  buildFileLenPacket, buildFileDataPacket, buildFileStopPacket,
  buildSleepTimePacket, buildStringPacket,
  formatDuration, parseBleResponse
};
