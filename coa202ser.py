import serial
import time
import argparse
import sys

# Call this program with
# python coa202ser.py portdevice timeout
# e.g. on windows:
#      python coa202ser.py COM2
# or macos/linux:
#      python coa202ser.py /dev/ttyACM0
#
# Please note that if timeout (for serial) is omitted a default value of 5 (seconds) will be used
# Remember you cannot leave this python program running while uploading your compiled Arduino sketch to the Arduino
#

# The code below parses the command line arguments and leaves the
# variable args.port containing the device name.

parser = argparse.ArgumentParser(
    description='Talk to Ardunio over serial')
parser.add_argument(
    'port',
    metavar='device',
    type=str,
    nargs='?',
    help='The port to connect to')
parser.add_argument(
    'timeout',
    metavar='timeout',
    type=int,
    nargs='?',
    default=1,
    help='The timeout for the serial message')
args = parser.parse_args()

# Open the serial port (this will reset your Arduino)
print('connecting to port', args.port)
ser = serial.Serial(args.port, 9600, timeout=args.timeout,
                    rtscts=False, xonxoff=False, dsrdtr=False)

print("waiting for sync")
going = True
while going:
    s = ser.read(1)  # Read just one byte
    # print(s)       # Print it for debugging
    if s == b'Q':
        going = False
ser.write(b'X')
print("Sync")

line = ser.readline()
print(line)  # This should print BASIC or your extension list


# Build a list of messages to send
# the b'' notation creates byte arrays suitable for
# passing to ser.write().  ser.write() will not accept
# str variables.

msgs = [b'A-GR04XFB-C-GranbySt.', b'A-GF57XWD-B-BeehiveLane', b'A-BD51SMR-V-BrownsLane', b'S-GR04XFB-PD', b'T-GR04XFB-B', b'L-GF57XWD-Southfield', b'S-GF57XWD-1', b'L-CU57ABC-BeehiveLane', b'L-GR04XFB-BrownsLane', b'S-BD51SMR-PD', b'L-BD51SMR-BrownsLane', b'A-CU57ABC-C-BedfordSquare', b'A-CU57ABC-BedfordSq.', b'A-CU57ABC-C-BedfordSquare', b'A-CU57ABC-C-', b'A-CU57ABC-M-BedfordSq.', b'A-GR04XFB-B-BrownsLane', b'Z-GR04XFB-BrownsLane', b'ACU57ABC-V-BrownsLane', b'A-GF23WSN- L- BrownsLane']

# Simply write these messages out one by one
# Customise above and below as you see fit.

for msg in msgs:
    print("Write:", str(msg))
    ser.write(msg + b'\n') # Add Newline control character after each message
    still_reading = True
    while(still_reading):
        # Check for message back.  This will timeout after args.timeout seconds
        line = ""
        line = str(ser.readline())
        if (line[2:8].upper() == 'DEBUG:'): # Remember to send a DONE! message after sending all your DEBUG: messages
            print("Read: ", line)            
        elif (line[2:8].upper() == 'ERROR:'): # You do not need to send a DONE! message after sending an ERROR: message
            print("Read: ", line)
            still_reading = False
        elif (line[2:7].upper() == 'DONE!'):  # A DONE! message will make the python testing program send the next msg in msgs          
            still_reading = False