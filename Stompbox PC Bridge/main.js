#!/usr/bin/env node

// Stompbox PC Bridge
// Dan Efran 2023
//
// a slightly enhanced/customized version of code from
//  https://github.com/tttapa/Projects/blob/master/Arduino/NodeJS/SLIP


/* 
  A simple Node.js script that acts as a bridge between
  an Arduino or Teensy that uses the OSC (Open Sounc Control)
  protocol over SLIP (Serial Line Internet Protocol)
  and a program that uses the OSC protocol over UDP.

  Usage:

        node main.js [local-port [remote-port [remote-address]]]

    If no ports are specified, port 8888 is used for both local port
    and 9999 for remote port). The default remote address is 'localhost'. @#@ changed from original 8888/8888

    If a local port is specified, but no remote port or address, 
    port 9999 is used as the remote port, and the default remote address is 'localhost'.

    If a local port and a remote port are specified, but no remote address,
    'localhost' is used as remote address.

    When a UDP message on the local port is received, the remote address will
    be automatically updated to the sender of that UDP message. The remote port
    will not be altered.

    The local port corresponds to the 'send' port in the audio software.
    The remote port corresponds to the 'receive' port in the audio software.

    The script will automatically look for Arduino or Teensy boards connected
    via USB. If no such board can be find, the first COM port will be selected.
    You can specify a port to override the search for Arduinos.

  Dependencies:

    Node.js: https://nodejs.org/
    node-serialport: https://github.com/node-serialport/node-serialport

  https://github.com/tttapa/Projects/blob/master/Arduino/NodeJS/SLIP
*/

// how much output to terminal window? 0 = silent running, 1 = user, 2 = power user, 3 = debug
let verbose = 2;

let comName;
// comName = '/dev/ttyUSB0'; // Uncomment this line to select a specific port instead of searching for an Arduino.
const baudRate = 115200; // 9600; // 115200;

const defaultLocalPort = 8888;
const defaultRemotePort = 9999;
const defaultRemoteAddress = 'localhost';

// ------------ UDP ------------ //
//#region UDP

let localPort = defaultLocalPort;
if (process.argv.length > 2)
    localPort = parseInt(process.argv[2]);

let remotePort = defaultRemotePort;
if (process.argv.length > 3)
    remotePort = parseInt(process.argv[3]);

let remoteAddr = defaultRemoteAddress;
if (process.argv.length > 4)
    remoteAddr = process.argv[4];

const dgram = require('dgram');
const server = dgram.createSocket('udp4');

server.on('error', (err) => {
    console.log(`server error:\r\n${err.stack}`);
    server.close();
});

server.on('message', (message, rinfo) => {
	
	if (verbose >= 3) {
		console.log(`>>> ${message.join(' ')} (${rinfo.address}:${rinfo.port})`);
	} else if (verbose >= 2) {
		console.log(`>>> ${message.toString()}\n`); // @#@#@
	}

	
    remoteAddr = rinfo.address;
    sendSerial(message);
});

server.on('listening', () => {
    const address = server.address();
	if (verbose >= 2) {
		console.log(``);
		console.log(`- Server listening on port ${address.port}`);
	} else if (verbose >= 1) {
		console.log(`- OSC active`);
	}
});

server.bind(localPort);

function sendUDP(message) {
	
	if (verbose >= 3) {
		console.log(`<<< ${message.join(' ')} (${remoteAddr}:${remotePort})`);
	} else if (verbose >= 2) {
		console.log(`<<< ${message.toString()}\n`); // @#@#@
	}

    server.send(
        message, remotePort, remoteAddr,
        (err) => { if (err) console.error(`Unable to send UDP packet: ${err}`); }
    );
}

//#endregion

//console.log('Looking for Arduino...');

// ------------ Serial ------------ //
//#region Serial

const serialport = require("serialport");
const SerialPort = serialport.SerialPort;

let port;

if (comName) {
	console.log('Opening specified port (' + comName + ')');
    openPort(comName);
} else {
	
	//console.log('No port specified. Scanning...');
	
	let list = SerialPort.list();
	list.then( 
		(ports) => {
			//console.log('got some ports');
			if (ports.length == 0)
				console.error("No Serial ports found");
			
			
			// Iterate over all the serial ports, and look for an Arduino or Teensy
			ports.some((port) => {
				
				if (verbose >= 3) {
				console.log('checking port ' + port.path + '...');
				console.log(port)
				}
				
				// manufacturer isn't reliable; it might well be the USB I/O rather than the Arduino brain itself.
				// in any case I may have multiple arduino devices connected at times
				// so I just cheat and scan for my specific device by hardcoded serial number.
				let foundArduino = (
					port.serialNumber.match(/(?:.*2633961F.*)/) 
					
				)
					
				if (foundArduino) {
					comName = port.path;
					if (verbose >= 2) {
						console.log('- Found Stompbox');
						if (verbose >= 3) {
							console.log('\t' + port.path);
							console.log('\t\t' + port.pnpId);
							console.log('\t\t' + port.manufacturer);
							console.log('\t\t' + port.friendlyName);
						}
					}
					return true;
				}
				return false;
			});
			
			if (!comName) {
				comName = ports[0].path; // ports.length - 1
				console.warn('- No Arduino found, selecting first COM port (' + comName + ') of ' + ports.length);
			}
			
			if (verbose >= 2) {
				console.log('- Opening port ' + comName + '.');
			}
			openPort(comName);
		},
		(err) => {
			console.error(err);
		} );
}

function openPort(comName) {
    // Open the port
    port = new SerialPort( { path: comName, baudRate: baudRate } )
	
    // Attach a callback function to handle incomming data
    port.on('data', receiveSerial);
	if (verbose >= 1) {
		console.log("- Connected to Stompbox");
	}
}

//#endregion

// ----------- SLIP ------------ //
//#region SLIP
const SLIP = require('./SLIP.js');

const parser = new SLIP.SLIPParser;

function receiveSerial(dataBuf) {

    // Loop over all received bytes
    for (let i = 0; i < dataBuf.length; i++) {
        // Parse the byte
        let length = parser.parse(dataBuf[i])
        if (length > 0) {
            sendUDP(parser.message);
        }
    }
	
	if (verbose >= 3) {
		console.log('receiveSerial: length ' + dataBuf.length);
	}
	if (verbose >= 3) {
		for (let ii = 0; ii < dataBuf.length; ii++) {
			console.log('data: ' + dataBuf[ii]);
		}
		console.log('');
	}
	
}

const encoder = new SLIP.SLIPEncoder;

function sendSerial(dataBuf) {
	
    // Encode the data using the SLIP protocol
    encoder.encode(dataBuf);
    let out = Buffer.from(encoder.message);
    // Send the encoded data over the Serial port
    port.write(out);

	if (verbose >= 3) {
		console.log('sendSerial');
	}
}

//#endregion
