#!/usr/bin/env python
## First install
# sudo apt-get install python python-alsaaudio python-aubio
# sudo apt-get install libasound2-dev
# pip install pyalsaaudio

## Docs on libalsa
# https://larsimmisch.github.io/pyalsaaudio/libalsaaudio.html

## This is an example of a simple sound capture script.
##
## The script opens an ALSA pcm for sound capture. Set
## various attributes of the capture, and reads in a loop,
## Then prints the volume.
##
## To test it out, run it and shout at your microphone:

import alsaaudio, time, audioop
import numpy as np
import matplotlib.pyplot as plt
# import matplotlib

# Open the device in nonblocking capture mode. The last argument could
# just as well have been zero for blocking mode. Then we could have
# left out the sleep call in the bottom of the loop

## Nonblockin mode
# inp = alsaaudio.PCM(alsaaudio.PCM_CAPTURE,alsaaudio.PCM_NONBLOCK)

# Normal blocking mode
inp = alsaaudio.PCM(alsaaudio.PCM_CAPTURE)

# Set attributes: Mono, 8000 Hz, 16 bit little endian samples
inp.setchannels(1)
inp.setrate(8000)
inp.setformat(alsaaudio.PCM_FORMAT_S16_LE)

# The period size controls the internal number of frames per period.
# The significance of this parameter is documented in the ALSA api.
# For our purposes, it is suficcient to know that reads from the device
# will return this many frames. Each frame being 2 bytes long.
# This means that the reads below will return either 320 bytes of data
# or 0 bytes of data. The latter is possible if we are in nonblocking
# mode.
inp.setperiodsize(160)

max_buff_size = 8000
buffer = np.array([])

# import Queue
# data_q = Queue.Queue()
plt.figure(1)
plt.show(block=False)


while True:
    # Read data from device
    l,data = inp.read()
    decoded_block = np.frombuffer(data, dtype='i2')
    buffer = np.append(buffer, decoded_block)
    # buffer = buffer.append(decoded_block)
    buffer = buffer[-max_buff_size:]
    if buffer.size == max_buff_size:
        plt.specgram(buffer, NFFT=256, Fs=8000)
        plt.pause(0.001)
        plt.draw()
        buffer = np.array([])

    # print(decoded_block, 'dtype:', type(decoded_block))
    if l:
        # Return the maximum of the absolute value of all samples in a fragment.
        print(audioop.max(data, 2))
    time.sleep(.001)