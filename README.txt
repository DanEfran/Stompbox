
-- Stompbox --

A hardware control surface for use with Reaper DAW over OSC, to emulate a guitar pedalboard.

-----

To use:

 - plug in Stompbox device. (It will flash its lights in a brief startup sequence.)
 - open a command prompt window and run 'bridge.bat'
 - stomp any button a few times to wake up the connection
 - open track 1 fx window. Take note of the fx bypass settings.
	stomp each button (including pressing in on the knobs!) a few times 
	to sync each stompbox lamps to the corresponding bypass checkbox in the DAW. 
	(The device can't presently read these settings without writing them.)
	Set initial bypass configuration as desired from the device.
 - Open TS-999 plugin; take note of the knob settings.
	Twiddle each of the stompbox device's knobs to wake them up, 
	then set the knob settings back to the way they were (or however you want them)
 - Now you are ready to use buttons 1-4 and the knob buttons to turn effects 2-8 on and off,
	button 5 to cycle through three amp channels on The Anvil at fx index 5,
	the external pedal to control NA Wah at fx index 2,
	the knobs to control the drive, tone, and level of the TS-999 SubScreamer at fx index 4,
	and the record button to toggle recording.
 - To customize the control assignments -- e.g. if you move the FX around in the list -- you
	currently must edit the code and re-upload to the device (e.g. from the Arduino IDE).


-----

Troubleshooting:

 - make sure bridge is connecting to device
 - use bridge's text output and Reaper DAW's OSC Preferences -> Edit -> Listen to check connection
 - Stompbox PC Bridge/main.js line 49 offers several levels of verbosity for bridge's text output
 
