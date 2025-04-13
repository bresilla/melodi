import subprocess
import json

Import("env")

# nodeid in ipv6 format
ipv6_first = 0x00
ipv6_last = 0x65
board_serial = None

port = env.subst("$UPLOAD_PORT")
if port:
    try:
        output = subprocess.check_output(["pio", "device", "list", "--json-output"])
        devices = json.loads(output.decode("utf-8"))
    except Exception as e:
        print("Error running pio device list:", e)

    for device in devices:
        if device.get("port") == port:
            hwid_str = device.get("hwid", "")
            if "SER=" in hwid_str:
                ser = hwid_str.split("SER=")[1].split()[0]
                board_serial = ser
                break


if board_serial:
    # ipv6_last = int(board_serial, 16)
    if len(board_serial) > 16:
        board_serial = board_serial[-16:]
    try:
        ipv6_last = int(board_serial, 16)
    except ValueError as e:
        print("Error converting serial to int:", e)
        ipv6_last = 0


# env.Append(CCFLAGS=["MY_NODE_ID=32"])
env.Append(CPPDEFINES=["IPV6_FIRST=0x%x" % ipv6_first, "IPV6_LAST=0x%x" % ipv6_last])
