#!/usr/bin/env python
from __future__ import print_function
import numpy as np
from scipy.io import wavfile
import csv
import sys
import argparse
import matplotlib.pyplot as plt

def convert_data(data):
    for k,key in enumerate(data.keys()):
        if key == 'id' or \
           key == 'timestamp' or \
           key == 'flag':
            data[key] = np.array([int(val) for val in data[key]])
        elif key == 'frames':
            data_stream = []
            for row in data[key]:
                data_stream.append([np.int16(val) for val in row.strip(' ').split(' ')])
            data[key] = np.concatenate(data_stream)
    return data



def read_csv(filename, header=True):
    with open(filename) as csv_file:
        out = {}
        header_list = {}
        csv_reader = csv.reader(csv_file, delimiter=',')
        line_count = 0
        for row in csv_reader:
            if line_count == 0:
                if header:
                    print("header: ", row)
                    for i,it in enumerate(row):
                        out[it] = []
                        header_list[i] = it
                else:
                    for i,it in enumerate(row):
                        out[i] = [it]
                        header_list[i] = i                   
                line_count += 1
            else:
                for i,it in enumerate(row):
                    out[header_list[i]].append(it)
                line_count += 1
        print("Total lines:", line_count)
        return out



def main(argv):
    # parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)
    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument(
        "filename",
        help="CSV filename"
    )
    parser.add_argument(
        "-w","--wavfilename",
        default="rec_mic_pytest.wav",
        help="Name of the output wav file for the frame stream"
    )
    parser.add_argument(
        "-p","--plot",
        action="store_false",
        help="Don't plot the graphs"
    )
    args = parser.parse_args()

    data = read_csv(args.filename)
    data = convert_data(data)

    wavfile.write(args.wavfilename, rate=44100, data=data["frames"])
    if args.plot:
        plt.figure(1)

        n = data["frames"].size
        Fs = 44100;  # sampling rate
        Ts = 1.0/Fs; # sampling interval

        k = np.arange(n)
        t = k*(1./Fs)
        T = n/Fs

        frq = k/T # two sides frequency range
        frq = frq[range(n/2)] # one side frequency range
        Y = np.fft.fft(data["frames"])/n # fft computing and normalization
        Y = Y[range(n/2)]

        fig, ax = plt.subplots(2, 1)
        ax[0].plot(data["frames"])
        ax[0].set_xlabel('Time')
        ax[0].set_ylabel('Amplitude')
        ax[1].plot(frq,abs(Y),'r') # plotting the spectrum
        ax[1].set_xlabel('Freq (Hz)')
        ax[1].set_ylabel('|Y(freq)|')

        plt.show(block=False)

    print(data)
    input("Press any key to exit ...")


if __name__ == '__main__':
    main(sys.argv)