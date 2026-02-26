# SprinklerMaster - Initial Setup Guide

## What You Need

- Raspberry Pi Pico 2W with SprinklerMaster firmware flashed
- A phone, tablet, or laptop with WiFi
- Your home WiFi network name (SSID) and password

## Step 1: Power On

Plug in the Pico 2W. On first boot (or with no WiFi configured), it will
automatically enter **Setup Mode**.

The LCD display will show:

```
Setup Mode
192.168.4.1
```

## Step 2: Connect to the Setup Network

On your phone or laptop, open WiFi settings and connect to:

- **Network name:** `SprinklerSetup`
- **Password:** None (open network)

> **Note:** Your device may warn that this network has no internet. That's
> expected -- stay connected.

## Step 3: Open the Setup Page

Open a web browser and navigate to:

```
http://192.168.4.1
```

You will see the SprinklerMaster WiFi Setup page.

## Step 4: Enter WiFi Credentials

1. Enter your home WiFi network name (SSID)
2. Enter your WiFi password
3. Tap **Save & Reboot**

The device will save the credentials and automatically reboot.

## Step 5: Reconnect to Your Home WiFi

Switch your phone/laptop back to your normal WiFi network. After about
30 seconds, the Pico will connect and the LCD will show its IP address
(e.g., `192.168.1.42`).

You can also access the dashboard at:

```
http://sprinkler.local
```

> **Note:** `.local` addresses use mDNS which works on most devices. If it
> doesn't resolve, use the IP address shown on the LCD.

## Step 6: Set Your Timezone

1. Open the dashboard in your browser
2. Click **Settings** in the Quick Actions section
3. Select your timezone from the dropdown
4. Click **Save Timezone**

This takes effect immediately -- no reboot needed.

## Step 7: Configure Zones

1. From the dashboard, click **Zone Settings**
2. Name each zone to match your sprinkler layout (e.g., "Front Lawn",
   "Garden Beds")
3. Disable any zones that aren't wired up
4. Click **Save Changes**

## Step 8: Create Schedules or Programs

- **Schedules** run a single zone at a set time (weekly or every N days)
- **Programs** run multiple zones in sequence at a set time

Use **Schedule Manager** or **Program Manager** from the dashboard to set
these up.

## Troubleshooting

### WiFi connection fails after setup

If the device can't connect to your WiFi (wrong password, network out of
range, etc.), it will automatically fall back to Setup Mode. Repeat from
Step 2.

### Need to change WiFi credentials later

1. Open the dashboard and go to **Settings**
2. Enter new SSID and password, then click **Save WiFi Credentials & Reboot**

### Need to start over completely

1. Open **Settings** from the dashboard
2. Click **Clear WiFi & Enter Setup Mode**
3. The device reboots into AP mode -- repeat from Step 2

### LCD shows "Network Failed"

The WiFi chip failed to initialize. Try power cycling the device. If it
persists, re-flash the firmware.

### RTC Battery Warning on dashboard

The DS3231 backup battery may be dead or was recently replaced. The warning
clears automatically after the first NTP time sync. If it persists across
reboots, replace the coin cell battery (CR2032).
