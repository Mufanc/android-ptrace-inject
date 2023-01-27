PID="$1"
APK_PATH=$(pm path $(cat /proc/$PID/cmdline) | sed 's/package://')
LIB_PATH=$(dirname "$APK_PATH")/lib/hook/libhack.so
mkdir -p $(dirname "$LIB_PATH")
cp hack.so "$LIB_PATH"
chmod 666 "$LIB_PATH"
logcat -c
./injector $PID "$LIB_PATH"
pmap "$PID" | grep libhack.so
sleep 3
rm -r $(dirname "$LIB_PATH")
logcat --pid "$PID" -v raw -v color
