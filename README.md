# DSload

DSload is a basic but fairly reliable send-only file transfer utility that allows you to send files (such as NDS files, resources, etc) from your PC to your Nintendo DS via WiFi for those who don't have a hbmenu compatible flash card, eliminating the need of removing your flash card whenever you need to update files of your project.

Despite its namesake, DSload currently does not support launching of NDS files but it may be implemented in a future update.

[Download Binary Here (v0.25)](http://lameguy64.github.io/dsload-0.25.zip)

## Usage instructions:

* Extract dsload.nds into your flash card of choice for your DS.
* Make sure you have WFC settings on your DS properly configured to connect to your router, you can use any Nintendo WFC compatible game to change the WFC settings.
* Launch dsload.nds on your DS, it should display your DS's IP address once it successfully initializes WiFi and has connected to your router.
* Use the PC based command line tool to send files to your DS, specify the IP address of your DS with the -ip switch (or DSLOAD environment variable) along with the names of files you wish to send.

## Tips/Notes:

* Configure your router's DHCP settings and assign a fixed IP address for your DS by its MAC address. You may also want to set a DSLOAD environment variable with a value of your DS's IP address.
* Using the -shutdown argument on the PC side tool would either shutdown the DS or return you to your flash card's menu once files have been sent to the console depending on the flash card or firmware you use.

## Planned and Upcoming Features:

- [ ] Option to launch homebrew NDS files once sent to DS.
- [ ] Automated directory path creation (tried to implement but failed due to buggy directory management on my R4 Upgrade clone card).
- [ ] Support for wildcards and directory parsing.
- [ ] CRC based file compare system so files that have not changed will not be sent.
