# OpenGD77
Firmware for DMR transceivers using the STM32F405VGT MCU, AT1846S RF chip and HR-C6000 DMR chipset.  
Including the Radioddiy TYT MD-380UV MD-390UV/ Retevis RT-3S and Baofeng DM-1701 / Retevis RT-84

# Project status

The firmware is relatively stable and provides DMR and FM audio transmission and reception, as well as a DMR hotspot mode. Testing has only occurred on the TYT MD-UV390 5w, but should work fine on other radios.

However it does not support some core functionality that the official firmware supports, including sending and receiving of text / SMS messages.

The firmware source code does not contain a AMBE codec required for DMR operation.  
This functionality is provided by the official firmware which is merged with the OpenGD77 by the OpenGD77CPS or firmware loader.

The MD9600 source code is provided in the tree above so it can be easily located later.

The "patcher" folder contains a script which pulls the most recent OPENGD77 files and applies the changes of super90. For building the firmware you only need the MDUV380 firmware folder. The firmware is built using the STM IDE. 

It is unlikely this will be ported to the GD-77 branch of opengd77 radios. It takes up a considerable amount of storage which isn't available on GD-77 radios.

# User guide

See https://github.com/LibreDMR/OpenGD77_UserGuide for most functionality, changes below.

# Super90 Unique Features

The most obvious first change to the opengd77 firmware is the inclusion of AES256 encryption. This is accessed via rhe 'Enc Key' menu item. There are 16 possible keys, which can be created by using 32 character passphrases. Once saved the passphrase is unrecoverable, but some characters are shown on the screen to aid in remembering which slot is which. Once a key is entered, in the channel menu on a digital channel encryption can be enabled and one of the 16 keys can be selected. If a slot is selected that doesn't contain a key encryption will not be enabled. When encryption is enabled a "ENC" key is displayed in the bottom right of the screen. If you have an empty keyslot selected and transmit a note indicated that the keyslot is empty is displayed on the screen. 

*NOTE ENCRYPTION IS FOR EXPERIMENTAL USES ONLY AND MAY BE ILLEGAL TO USE IN YOUR AREA USER DISCRETION IS PARAMOUNT AND NO RESPONSIBILITY FOR USER ACTIONS RESTS WITH THE DEVELOPER*

In addition to AES256 Encryption the M17 digital mode has been added. The digital mode is accessed and used with the same keys and locations that DMR/FM selection is found in the base opengd77 firmware. 

The firmware is flashed using the same method as opengd77.

# Pending Improvements 
Units selection for the GPS screen, and a point tracker system for saving GPS coordinates and calculating travel distance. Encryption for M17 mode.

# Copyright

 The firmware is not copy written or proprietary in anyway. See individual source files for any additional copyright information copyright information.

## MCU SDK and API code:   
   See license files in sub-folders
	

