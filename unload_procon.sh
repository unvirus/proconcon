#!/bin/bash
if [ $USER != "root" ]; then
  echo "Root privilege is required."
  exit 0
fi

cd /sys/kernel/config/usb_gadget
echo "" > UDC
rm procon/configs/c.1/hid.usb0
rmdir procon/configs/c.1/strings/0x409
rmdir procon/configs/c.1
rmdir procon/functions/hid.usb0
rmdir procon/strings/0x409
rmdir procon
cd $HOME

