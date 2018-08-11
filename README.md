# Package deals with using acoustic data for road condition identification

## Microphone reading thread
micread_main.cpp - an example on how to use micread_thread
micread_thread.* - implementation of microphone reading thread using ALSA

assets/asoundrc  - copy it to ~/.asoundrc . This is a device config file for ALSA. It may work even without it.

## Other examples (for reference and testing only)
example_calsa_mic_recording.c - a simple C example of using ALSA
example_pyalsa_mic_recording.py - python ALSA example of reading a microphone (can plot a spectrogram)
example_pyaudio_mic_recording.py - pyaudio example of reading a microphone (did not work for me)

## Training an LSTM for identifying wet road
train_simple_puddle_classifier.py - training a classifier using wetness data from  https://lexfridman.com/wetroad/
