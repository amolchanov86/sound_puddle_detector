#!/usr/bin/env python
from __future__ import print_function
import pickle
import glob
import os
import librosa
import matplotlib.pyplot as plt
import tensorflow as tf
from tensorflow.python.ops import rnn, rnn_cell
import numpy as np
# %matplotlib inline
plt.style.use('ggplot')
from tqdm import tqdm

from scipy.io import wavfile
import csv
import sys
import argparse
import time
import datetime
import copy
import subprocess


def windows(data, window_size):
    start = int(0)
    while start < len(data):
        yield start, start + window_size
        start += int(window_size / 2)

def framewindows(data, window_size, shift=None):
    start = int(0)
    if shift is None:
        shift = int(window_size / 2)
    while start < len(data):
        yield start, start + window_size
        start += shift

def extract_features_only(file, file_ext="*.wav", bands = 20, frames = 41):
    window_size = 512 * (frames - 1)
    mfccs = []
    print('  loading file %s ...' % file)
    sound_clip,s = librosa.load(file, sr=44100)
    print("Rate:", s, "Sound clip: ", sound_clip, "dtype: ", sound_clip.dtype)
    print('  extracting features', end='')
    i = 0
    for (start,end) in tqdm(windows(sound_clip, window_size)):
        i += 1
        if i % 100 == 0: print('.',end='',flush=True)
        if (len(sound_clip[start:end]) == window_size):
            signal = sound_clip[start:end]
            mfcc = librosa.feature.mfcc(y=signal, sr=s, n_mfcc=bands).T.flatten()[:, np.newaxis].T
            mfccs.append(mfcc)
            
    features = np.asarray(mfccs).reshape(len(mfccs),frames,bands)
    return np.array(features)

def extract_features(data, timestamps, bands = 20, frames = 41, shift=8):
    """
    Assumes buffer (frame) size 512
    """
    window_size = (frames - 1)
    mfccs = []
    feat_timestamps = []
    i = 0
    s = 44100.
    # s = 22050.
    # print("data: ", type(data))
    for (start,end) in tqdm(framewindows(data, window_size, shift)):
        # print(start, end)c
        i += 1
        if i % 100 == 0: print('.',end='',flush=True)
        if (len(data[start:end]) == window_size):
            signal = frames2stream(data[start:end]) #original 44100 signal
            signal = np.array(signal).astype(np.float)
            # signal = signal[::2] #Subsampling to 22050
            signal = signal / 32768.
            mfcc = librosa.feature.mfcc(y=signal, sr=s, n_mfcc=bands).T.flatten()[:, np.newaxis].T
            mfccs.append(mfcc)
        else:
            print("WARN: wrong slice size")
        feat_timestamps.append(timestamps[min(end, len(timestamps)-1)])

    features = np.asarray(mfccs).reshape(len(mfccs),frames,bands)
    return np.array(features), np.array(feat_timestamps)


def frames2stream(data_frames):
    data_stream = []
    for row in data_frames:
        data_stream.append([np.int16(val) for val in row.strip(' ').split(' ')])
    return np.concatenate(data_stream)

def frames2stream_with_timestamps(data_frames, timestamps):
    data_stream = []
    timestamps_interpolated = []
    dt = 1.0/44100
    for frame_id, frame in tqdm(enumerate(data_frames)):
        frame_flat = [np.int16(val) for val in frame.strip(' ').split(' ')]
        data_stream.append(frame_flat)
        if frame_id < len(timestamps) - 1:
            dt = (timestamps[frame_id + 1] - timestamps[frame_id]) / (len(timestamps) - 1)
            interm_timestamps = np.linspace(timestamps[frame_id], timestamps[frame_id+1], len(frame_flat))
        else:
            interm_timestamps = np.linspace(timestamps[frame_id], timestamps[frame_id] + len(frame_flat)*dt, len(frame_flat))
        timestamps_interpolated.append(interm_timestamps) 

    return np.concatenate(data_stream), np.concatenate(timestamps_interpolated)

def convert_timestamp(data_time):
    data_time_ = np.array([int(val) for val in data_time])
    return data_time_


def convert_datetimestamp(data_time):
    #convert starts into microseconds
    for start_id, start in enumerate(data_time):
        if start.find(":") > 0:
            pt =datetime.datetime.strptime(start,"%H:%M:%S.%f")
            total_seconds = pt.second+pt.minute*60+pt.hour*3600
            total_mcs = total_seconds * 1000000 + pt.microsecond
        else:
            total_mcs = int(float(start) * 1000000)
        data_time[start_id] = total_mcs
    return data_time


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


def main():
    parser = argparse.ArgumentParser(
        description="Sanity check for learned RNN models from microphone data",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    # parser.add_argument(
    #     "indir",
    #     help="Name of the folder containing csv file(s) + timelabels.txt files (should be in the same folder as csv)"
    # )
    args = parser.parse_args()


    ##################################
    ## PARAMS
    frames = 41
    input_shape = (None, frames, 20)
    model_name = "_data/thunderhill/mic_rode__pred_rnn__classes_dry-wet/micpred_rnn__ep_0__iou_0.000__acc_0.995.meta"
    

    prepickled_data_filename = None
    # prepickled_data_filename = '_data/thunderhill/thunderhill_2018_08_22/wetness_data.pkl'

    wav_file = None
    # wav_file = "_data/thunderhill/thunderhill_2018_08_22/source/dry/2018_Aug_22_13:20:45__dry__slowspeed__short/mic_rec.wav" 
    # wav_label = 0 
    wav_file = "_data/thunderhill/thunderhill_2018_08_22/source/wet/2018_Aug_22_15:16:34__wet__notelemetry/mic_rec.wav"
    wav_label = 1 

    # indir = "_data/thunderhill/thunderhill_2018_08_22/source/dry/2018_Aug_22_13:20:45__dry__slowspeed__short"
    indir = "_data/thunderhill/thunderhill_2018_08_22/source/wet/2018_Aug_22_15:16:34__wet__notelemetry"
    csv_file = indir + os.sep + "mic_rec.csv"
    csv_label = 1


    ##################################
    ## FEATURES
    if wav_file is not None:
        print("Extracting features from wav file: ", wav_file)
        ts_features = extract_features_only(wav_file)
        ts_labels = np.zeros([ts_features.shape[0], 2])
        ts_labels[:, wav_label] = 1

    elif prepickled_data_filename is not None:
        print('Loading pickled data %s ...' % prepickled_data_filename)
        with open(prepickled_data_filename, 'rb') as pkl_file:
            data_dic = pickle.load(pkl_file)
            tr_features = data_dic['tr_features']
            tr_labels = data_dic['tr_labels']
            ts_features = data_dic['ts_features']
            ts_labels = data_dic['ts_labels']
    else:
        print("Extracting features from csv file: ", csv_file)
        ## Load csv file with audio
        csv_audio_data = read_csv(csv_file)
        csv_timestamps = convert_timestamp(csv_audio_data["timestamp"])
        ## Get features and labels
        time_and_labels = read_csv(indir + os.sep + "timelabels.txt")
        label_timestamps = convert_datetimestamp(time_and_labels["start_time"])

        ## Extranct features and timestamps of the features
        ts_features,feat_time = extract_features(
            data=csv_audio_data["frames"], 
            timestamps=csv_timestamps)
        ts_labels = np.zeros([ts_features.shape[0], 2])
        ts_labels[:, csv_label] = 1

    #Shuffling
    shuffled_axis = np.arange(ts_features.shape[0])
    np.random.shuffle(shuffled_axis)
    ts_features = ts_features[shuffled_axis, :]
    ts_labels = ts_labels[shuffled_axis, :]

    ##################################
    ## Load NN
    sess = tf.Session()
    # model_name = "data/thunderhill/mic_rode__pred_rnn__classes_dry-wet/micpred_rnn__ep_1__iou_0.989__acc_0.995.meta"
    checkpoint_name = model_name[:-5]

    saver = tf.train.import_meta_graph(model_name)
    saver.restore(sess, checkpoint_name)
    x = tf.get_default_graph().get_tensor_by_name("x:0")
    y = tf.get_default_graph().get_tensor_by_name("y:0") #labels
    pred = tf.get_default_graph().get_tensor_by_name("pred:0")
    accuracy = tf.get_default_graph().get_tensor_by_name("accuracy:0")


    ## Predict all frames
    print("Predicting features ", ts_features.shape," ...")
    pred_start_time = time.time()
    result = sess.run([pred, accuracy], feed_dict={x:ts_features, y:ts_labels})
    # print("Labels: ", ts_labels, " Pred: ", np.argmax(result[0], axis=1))
    print("Prediction time: ", time.time() - pred_start_time)
    pred_val = result[0]
    accuracy_val = result[1] 
    print("Accuracy: ", accuracy_val)



if __name__ == '__main__':
    main()

