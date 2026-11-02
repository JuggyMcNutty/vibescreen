#!/bin/sh

yellow=`echo "\033[01;33m"`
green=`echo "\033[01;32m"`
red=`echo "\033[01;31m"`
white=`echo "\033[m"`

BACKUP_DIR=/usr/data/guppyify-backup
K1_GUPPY_DIR=/usr/data/guppyscreen
FT2FONT_PATH=/usr/lib/python3.8/site-packages/matplotlib/ft2font.cpython-38-mipsel-linux-gnu.so
ASSET_NAME="guppyscreen"
LOCAL_ARCHIVE=""

while [ "$#" -gt 0 ]; do
    case "$1" in
        zbolt)
            ASSET_NAME="$ASSET_NAME-zbolt"
            shift
            ;;
        --archive)
            if [ "$#" -lt 2 ] || [ -z "$2" ]; then
                echo "usage: $0 [zbolt] [--archive path]" >&2
                exit 1
            fi
            LOCAL_ARCHIVE=$2
            shift 2
            ;;
        *)
            echo "usage: $0 [zbolt] [--archive path]" >&2
            exit 1
            ;;
    esac
done

ARCH=`uname -m`
if [ "$ARCH" = "mips" ] && [ -f /sys/class/graphics/fb0/virtual_size ]; then
    res=`cat /sys/class/graphics/fb0/virtual_size`
    x=${res%,*}
    y=${res#*,}

    printf "${green}Found screen virtual size $x, $y ${white}\n"
    if [ $y -lt 800 ] && [ $x -lt 800 ]; then
	printf "${green}Using smallscreen version ${white}\n"
	ASSET_NAME="guppyscreen-smallscreen"
    fi

else
    printf "${red}Unable to find compatible platform/screen size (found platform: $ARCH) ${white}\n"
    exit 1
fi

printf "${green}=== Installing VibeScreen === ${white}\n"

echo "Checking for a working Moonraker"
MRK_KPY_OK=`curl localhost:7125/server/info 2> /dev/null | jq .result.klippy_connected`
if [ "$MRK_KPY_OK" != "true" ]; then
    printf "${yellow}Moonraker is not properly setup at port 7125. Continue anyways? (y/n) ${white}\n"
    read confirm
    echo

    if [ "$confirm" = "y" -o "$confirm" = "Y" ]; then
        echo "Continuing to install VibeScreen"
    else
        echo "Please fix Moonraker and restart this script."
        exit 1
    fi
fi

KLIPPER_PATH=`curl localhost:7125/printer/info 2> /dev/null | jq -r .result.klipper_path`
if [ -z "$KLIPPER_PATH" -o x"$KLIPPER_PATH" == x"null" ]; then
    KLIPPER_PATH=/usr/share/klipper
    printf "${green} Falling back to klipper path: $KLIPPER_PATH ${white}\n"
fi

printf "${green} Found klipper path: $KLIPPER_PATH ${white}\n"

KLIPPY_EXTRA_DIR=$KLIPPER_PATH/klippy/extras
GCODE_SHELL_CMD=$KLIPPY_EXTRA_DIR/gcode_shell_command.py
SHAPER_CONFIG=$KLIPPY_EXTRA_DIR/calibrate_shaper_config.py

K1_CONFIG_FILE=`curl localhost:7125/printer/info 2> /dev/null | jq -r .result.config_file`
if [ -z "$K1_CONFIG_FILE" -o x"$K1_CONFIG_FILE" == x"null" ]; then
    K1_CONFIG_DIR=/usr/data/printer_data/config
    printf "${green} Falling back to config dir: $K1_CONFIG_DIR ${white}\n"
else
    K1_CONFIG_DIR=$(dirname "$K1_CONFIG_FILE")
    printf "${green} Found config dir: $K1_CONFIG_DIR ${white}\n"
fi

if [ -n "$LOCAL_ARCHIVE" ]; then
    if [ ! -f "$LOCAL_ARCHIVE" ]; then
        printf "${red}Local archive not found: $LOCAL_ARCHIVE ${white}\n"
        exit 1
    fi
    printf "${green} Installing local archive: $LOCAL_ARCHIVE ${white}\n"
    cp "$LOCAL_ARCHIVE" /tmp/guppyscreen.tar.gz || {
        printf "${red}Could not copy local archive ${white}\n"
        exit 1
    }
else
    # Every push to main publishes its own release, so latest is the newest build.
    ASSET_URL="https://github.com/JuggyMcNutty/vibescreen/releases/latest/download/$ASSET_NAME.tar.gz"

    printf "${green} Downloading asset: $ASSET_NAME.tar.gz ${white}\n"

    # Python reaches GitHub without a third-party curl binary.
    python3 -c "
import shutil, sys, urllib.request
with urllib.request.urlopen(sys.argv[1], timeout=180) as r, open(sys.argv[2], 'wb') as f:
    shutil.copyfileobj(r, f)
" "$ASSET_URL" /tmp/guppyscreen.tar.gz || {
        printf "${red}Download failed from $ASSET_URL ${white}\n"
        exit 1
    }
fi

# Check before unpacking over anything already installed.
if ! tar tzf /tmp/guppyscreen.tar.gz > /dev/null 2>&1; then
    printf "${red}Downloaded file is not a valid archive ${white}\n"
    exit 1
fi

if ! tar tzf /tmp/guppyscreen.tar.gz 2>/dev/null | grep -q 'guppyscreen/restore-guppyscreen.sh$'; then
    printf "${red}Downloaded archive has no recovery script ${white}\n"
    exit 1
fi

ORIGINAL_BACKUP=$BACKUP_DIR/guppyscreen-original.tar.gz
if [ -d "$K1_GUPPY_DIR" ]; then
    mkdir -p "$BACKUP_DIR"
    if [ -f "$ORIGINAL_BACKUP" ]; then
        if ! tar tzf "$ORIGINAL_BACKUP" 2>/dev/null | grep -qx 'guppyscreen/guppyscreen'; then
            printf "${red}Saved VibeScreen backup is invalid. Installation cancelled. ${white}\n"
            exit 1
        fi
    else
        BACKUP_TEMP=$ORIGINAL_BACKUP.tmp.$$
        if ! (umask 077 && tar czf "$BACKUP_TEMP" -C /usr/data guppyscreen); then
            rm -f "$BACKUP_TEMP"
            printf "${red}Could not save the current VibeScreen. Installation cancelled. ${white}\n"
            exit 1
        fi
        if ! tar tzf "$BACKUP_TEMP" 2>/dev/null | grep -qx 'guppyscreen/guppyscreen'; then
            rm -f "$BACKUP_TEMP"
            printf "${red}Could not verify the saved VibeScreen. Installation cancelled. ${white}\n"
            exit 1
        fi
        chmod 600 "$BACKUP_TEMP"
        if ! mv "$BACKUP_TEMP" "$ORIGINAL_BACKUP"; then
            rm -f "$BACKUP_TEMP"
            printf "${red}Could not finalize the backup. Installation cancelled. ${white}\n"
            exit 1
        fi
        printf "${green}Saved the previous VibeScreen in $ORIGINAL_BACKUP ${white}\n"
    fi
else
    printf "${yellow}No previous VibeScreen directory found; no app rollback is available. ${white}\n"
fi

tar xf /tmp/guppyscreen.tar.gz -C /usr/data/
if [ ! -f "$K1_GUPPY_DIR/restore-guppyscreen.sh" ]; then
    printf "${red}Recovery script missing after extraction. ${white}\n"
    exit 1
fi
cp -p "$K1_GUPPY_DIR/restore-guppyscreen.sh" /usr/data/restore-guppyscreen.sh
chmod 700 /usr/data/restore-guppyscreen.sh

if [ ! -f "$K1_GUPPY_DIR/guppyscreen" ]; then
    printf "${red}Did not find the VibeScreen binary in $K1_GUPPY_DIR. ${white}\n"
    exit 1
fi

#### let's see if guppyscreen starts before doing anything more
printf "${green} Test starting VibeScreen ${white}\n"
[ -f /etc/init.d/S99guppyscreen ] && /etc/init.d/S99guppyscreen stop &> /dev/null
killall -q guppyscreen
$K1_GUPPY_DIR/guppyscreen &> /dev/null &

## allow guppy to live a little
sleep 1

ps auxw | grep guppyscreen | grep -v sh | grep -v grep

if [ $? -eq 0 ]; then
    printf "${green} VibeScreen started successfully, continuing with installation ${white}\n"
    killall -q guppyscreen
else
    printf "${red} VibeScreen FAILED to start, aborting ${white}\n"
    exit 1
fi

printf "${green}Setting up VibeScreen printer macros ${white}\n"
if [ ! -f $GCODE_SHELL_CMD ]; then
    printf "${green}Installing gcode_shell_command.py for klippy ${white}\n"
    cp $K1_GUPPY_DIR/k1_mods/gcode_shell_command.py $GCODE_SHELL_CMD
fi

mkdir -p $K1_CONFIG_DIR/GuppyScreen/scripts
cp $K1_GUPPY_DIR/scripts/*.cfg $K1_CONFIG_DIR/GuppyScreen
cp $K1_GUPPY_DIR/scripts/*.py $K1_CONFIG_DIR/GuppyScreen/scripts

# cp onto a file that already exists keeps that file's mode, so a copy which
# once arrived without its execute bit keeps it through every reinstall. The
# two scripts gcode_shell_command runs are execed directly, so the mode is
# part of the install rather than a detail of the tarball.
chmod a+x $K1_CONFIG_DIR/GuppyScreen/scripts/calibrate_shaper.py \
         $K1_CONFIG_DIR/GuppyScreen/scripts/graph_belts.py

## includ guppyscreen *.cfg in printer.cfg
if grep -q "include GuppyScreen" $K1_CONFIG_DIR/printer.cfg ; then
    echo "printer.cfg already includes VibeScreen printer macros"
else
    printf "${green}Including VibeScreen printer macros in printer.cfg ${white}\n"
    sed -i '/\[include gcode_macro\.cfg\]/a \[include GuppyScreen/*\.cfg\]' $K1_CONFIG_DIR/printer.cfg
fi

## symlink usb
K1_GCODE_DIR=$(dirname "$K1_CONFIG_DIR")/gcodes
ln -sf /tmp/udisk $K1_GCODE_DIR/usb

printf "${green} Backing up original K1 files ${white}\n"
mkdir -p "$BACKUP_DIR"

if [ -f /etc/init.d/S12boot_display ] && [ ! -f "$BACKUP_DIR/S12boot_display" ]; then
    mv /etc/init.d/S12boot_display "$BACKUP_DIR/"
fi
if [ -f /etc/init.d/S50dropbear ] && [ ! -f "$BACKUP_DIR/S50dropbear" ]; then
    cp /etc/init.d/S50dropbear "$BACKUP_DIR/"
fi
if [ -f /etc/init.d/S99start_app ] && [ ! -f "$BACKUP_DIR/S99start_app" ]; then
    cp /etc/init.d/S99start_app "$BACKUP_DIR/"
fi

if [ ! -f $BACKUP_DIR/ft2font.cpython-38-mipsel-linux-gnu.so ]; then
    # backup ft2font
    mv /usr/lib/python3.8/site-packages/matplotlib/ft2font.cpython-38-mipsel-linux-gnu.so $BACKUP_DIR
fi

## dropbear early to ensure ssh is started with display-server
cp $K1_GUPPY_DIR/k1_mods/S50dropbear /etc/init.d/S50dropbear

printf "${white}=== Do you want to disable all Creality services (revertable) with VibeScreen installation? ===\n"
printf "${green}  Pros: Frees up system resources on your K1 for critical services such as Klipper (Recommended)\n"
printf "${white}  Cons: Disabling all Creality services breaks Creality Cloud/Creality Slicer.\n\n"
printf "Disable all Creality Services? (y/n): "

read confirm_decreality
echo

if [ "$confirm_decreality" = "y" -o "$confirm_decreality" = "Y" ]; then
    printf "${green}Disabling Creality services ${white}\n"
    rm /etc/init.d/S99start_app
else
    # disables only display-server and Monitor
    mv /usr/bin/Monitor /usr/bin/Monitor.disable
    mv /usr/bin/display-server /usr/bin/display-server.disable
fi

printf "${green}Setting up VibeScreen ${white}\n"
cp $K1_GUPPY_DIR/k1_mods/S99guppyscreen /etc/init.d/S99guppyscreen

cp $K1_GUPPY_DIR/k1_mods/calibrate_shaper_config.py $SHAPER_CONFIG

ln -sf $K1_GUPPY_DIR/k1_mods/guppy_module_loader.py $KLIPPY_EXTRA_DIR/guppy_module_loader.py
ln -sf $K1_GUPPY_DIR/k1_mods/guppy_config_helper.py $KLIPPY_EXTRA_DIR/guppy_config_helper.py
ln -sf $K1_GUPPY_DIR/k1_mods/tmcstatus.py $KLIPPY_EXTRA_DIR/tmcstatus.py


if [ ! -d "/usr/lib/python3.8/site-packages/matplotlib-2.2.3-py3.8.egg-info" ]; then
    echo "Not replacing mathplotlib ft2font module. PSD graphs might not work"
else
    printf "${green}Replacing mathplotlib ft2font module for plotting PSD graphs ${white}\n"
    cp $K1_GUPPY_DIR/k1_mods/ft2font.cpython-38-mipsel-linux-gnu.so $FT2FONT_PATH
fi

ln -sf $K1_GUPPY_DIR/k1_mods/respawn/libeinfo.so.1 /lib/libeinfo.so.1
ln -sf $K1_GUPPY_DIR/k1_mods/respawn/librc.so.1 /lib/librc.so.1


sync

if [ ! -f $K1_GUPPY_DIR/guppyscreen ]; then
    printf "${red}Installation failed, did not find guppyscreen in $K1_GUPPY_DIR. Make sure to extract the guppyscreen directory in /usr/data. ${white}\n"
    exit 1
fi

## double check dropbear is the correct one
if ! diff $K1_GUPPY_DIR/k1_mods/S50dropbear /etc/init.d/S50dropbear > /dev/null ; then
    printf "${red}Dropbear (SSHD) didn't install properly. ${white}\n"
    exit 1
fi

## request to reboot
printf "Restart Klipper now to pick up the new changes (y/n): "
read confirm
echo

if [ "$confirm" = "y" -o "$confirm" = "Y" ]; then
    echo "Restarting Klipper"
    /etc/init.d/S55klipper_service restart
else
    printf "${red}Some VibeScreen functionality won't work until Klipper is restarted. ${white}\n"
fi

killall -q Monitor
killall -q display-server
if [ "$confirm_decreality" = "y" -o "$confirm_decreality" = "Y" ]; then
    echo "Killing Creality services"
    killall -q master-server
    killall -q audio-server
    killall -q wifi-server
    killall -q app-server
    killall -q upgrade-server
    killall -q web-server
fi

printf "${green}Starting VibeScreen ${white}\n"
/etc/init.d/S99guppyscreen restart &> /dev/null

sleep 1

ps auxw | grep guppyscreen | grep -v sh | grep -v grep

if [ $? -eq 0 ]; then
    printf "${green} Successfully installed VibeScreen. Enjoy! ${white}\n"
else
    printf "${red} VibeScreen FAILED to install. Rolling back... ${white}\n"
    cp $BACKUP_DIR/S99start_app /etc/init.d/S99start_app
    rm /etc/init.d/S99guppyscreen

    /etc/init.d/S99start_app restart &> /dev/null
    exit 1
fi
