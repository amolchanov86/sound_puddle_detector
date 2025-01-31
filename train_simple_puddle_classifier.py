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

def windows(data, window_size):
    start = int(0)
    while start < len(data):
        yield start, start + window_size
        start += int(window_size / 2)


# try:
#
# except Exception as e:
#     for k, v in locals().items():
#         print(k, ':', v)
#     raise ValueError('exception: %s' % str(e))


def extract_features(parent_dir, sub_dirs, label_names, file_ext="*.wav", bands = 20, frames = 41):
    window_size = 512 * (frames - 1)
    mfccs = []
    labels = []
    for l, sub_dir in enumerate(sub_dirs):
        for fn in glob.glob(os.path.join(parent_dir, sub_dir, file_ext)):
            print('')
            print('  loading file %s ...' % fn)
            sound_clip,s = librosa.load(fn)
            # label = fn.split('/')[2].split('-')[1]
            label_name = fn.rsplit(os.sep, 1)[1].rsplit('.', 1)[0].split('_')[1]
            label = label_names[label_name]
            print('  extracting features', end='')
            i = 0
            for (start,end) in windows(sound_clip, window_size):
                i += 1
                if i % 100 == 0: print('.',end='',flush=True)
                if (len(sound_clip[start:end]) == window_size):
                    signal = sound_clip[start:end]
                    mfcc = librosa.feature.mfcc(y=signal, sr=s, n_mfcc=bands).T.flatten()[:, np.newaxis].T
                    mfccs.append(mfcc)
                    labels.append(label)
    features = np.asarray(mfccs).reshape(len(mfccs),frames,bands)
    return np.array(features), np.array(labels, dtype = np.int)

def extract_features_lbldirs(parent_dir, sub_dirs, label_names, file_ext="*.wav", bands = 20, frames = 41):
    """
    Labels = foldernames
    """
    window_size = 512 * (frames - 1)
    mfccs = []
    labels = []
    for l, sub_dir in enumerate(sub_dirs):
        for fn in glob.glob(os.path.join(parent_dir, sub_dir, file_ext)):
            print('')
            print('  loading file %s ...' % fn)
            sound_clip,s = librosa.load(fn)
            # label = fn.split('/')[2].split('-')[1]
            label_name = fn.rsplit(os.sep, 1)[1].rsplit('.', 1)[0].split('_')[1]
            label = label_names[label_name]
            print('  extracting features', end='')
            i = 0
            for (start,end) in windows(sound_clip, window_size):
                i += 1
                if i % 100 == 0: print('.',end='',flush=True)
                if (len(sound_clip[start:end]) == window_size):
                    signal = sound_clip[start:end]
                    mfcc = librosa.feature.mfcc(y=signal, sr=s, n_mfcc=bands).T.flatten()[:, np.newaxis].T
                    mfccs.append(mfcc)
                    labels.append(label)
    features = np.asarray(mfccs).reshape(len(mfccs),frames,bands)
    return np.array(features), np.array(labels, dtype = np.int)

def one_hot_encode(labels):
    n_labels = len(labels)
    n_unique_labels = len(np.unique(labels))
    one_hot_encode = np.zeros((n_labels,n_unique_labels))
    one_hot_encode[np.arange(n_labels), labels] = 1
    return one_hot_encode

###########################################################
## Getting data
label_names = {'dry': 0, 'wet': 1}
label_max = len(label_names) - 1
parent_dir = '_data/sound_wetroad_dataset/source'
prepickled_data_filename = '_data/sound_wetroad_dataset/wetness_data.pkl'

if os.path.isfile(prepickled_data_filename):
    print('Loading pickled data %s ...' % prepickled_data_filename)
    with open(prepickled_data_filename, 'rb') as pkl_file:
        data_dic = pickle.load(pkl_file)
    tr_features = data_dic['tr_features']
    tr_labels = data_dic['tr_labels']
    ts_features = data_dic['ts_features']
    ts_labels = data_dic['ts_labels']

else:
    print('Extracting training features ...')
    tr_sub_dirs = ['dry1', 'dry2', 'wet1', 'wet2']
    tr_features,tr_labels = extract_features(parent_dir,tr_sub_dirs, label_names=label_names)
    tr_labels = one_hot_encode(tr_labels)

    print('Extracting test features ...')
    ts_sub_dirs = ['dry3', 'wet3']
    ts_features,ts_labels = extract_features(parent_dir,ts_sub_dirs, label_names=label_names)
    ts_labels = one_hot_encode(ts_labels)

print('Data shapes: train/test  data/lbl', tr_features.shape, tr_labels.shape, ts_features.shape, ts_labels.shape)
if np.any(np.isnan(tr_features)) or not np.all(np.isfinite(tr_features)):
    raise ValueError('Data contains nan or inf features ...')
print('Shuffling data ...')
def unison_shuffle(a, b):
    assert len(a) == len(b)
    p = np.random.permutation(len(a))
    return a[p], b[p]
# tr_features,tr_labels = unison_shuffle(tr_features,tr_labels)
# print('Data shapes after shuffling: train/test  data/lbl', tr_features.shape, tr_labels.shape, ts_features.shape, ts_labels.shape)
# print('Labels: ', tr_labels)

# Pickling data for the future
if not os.path.isfile(prepickled_data_filename):
    print('Saving data to %s ...' % prepickled_data_filename)
    data_dic = {'tr_features': tr_features, 'tr_labels': tr_labels,
                'ts_features': ts_features, 'ts_labels': ts_labels}
    try:
        with open(prepickled_data_filename, 'wb') as output_pkl:
            # Pickle dictionary using protocol 0.
            pickle.dump(data_dic, output_pkl)
    except Exception as e:
        os.remove(prepickled_data_filename)
        raise



print('Constructin NN graph ...')
#####################################################
## Contructing NN
tf.reset_default_graph()

learning_rate = 0.001
batch_size = 50
display_step = 200
tr_examples_num = tr_features.shape[0]
epoch_size = int(np.ceil(tr_examples_num / batch_size))
epochs_num = 100
training_iters = epoch_size

# Network Parameters
n_input = 20
n_steps = 41
n_hidden = 300
n_classes = len(label_names)

x = tf.placeholder(tf.float32, name="x", shape=[None, n_steps, n_input])
y = tf.placeholder(tf.float32, name="y", shape=[None, n_classes])

weight = tf.Variable(tf.random_normal([n_hidden, n_classes]))
bias = tf.Variable(tf.random_normal([n_classes]))

def RNN(x, weight, bias):
    cell1 = rnn_cell.LSTMCell(n_hidden, state_is_tuple=True)
    cell2 = rnn_cell.LSTMCell(n_hidden, state_is_tuple=True)
    cell = rnn_cell.MultiRNNCell([cell1, cell2])
    output, state = tf.nn.dynamic_rnn(cell, x, dtype = tf.float32)
    output = tf.transpose(output, [1, 0, 2])
    last = tf.gather(output, int(output.get_shape()[0]) - 1)
    return tf.nn.softmax(tf.matmul(last, weight) + bias)

def BRNN(x, weight, bias):
    cell1_fw = rnn_cell.LSTMCell(n_hidden, state_is_tuple=True)
    cell2_fw = rnn_cell.LSTMCell(n_hidden, state_is_tuple=True)
    cell_fw = rnn_cell.MultiRNNCell([cell1_fw, cell2_fw])

    cell1_bw = rnn_cell.LSTMCell(n_hidden, state_is_tuple=True)
    cell2_bw = rnn_cell.LSTMCell(n_hidden, state_is_tuple=True)
    cell_bw = rnn_cell.MultiRNNCell([cell1_bw, cell2_bw])

    output, out_states = tf.nn.bidirectional_dynamic_rnn(cell_fw, cell_bw, x, dtype = tf.float32)
    # print(output[-1].get_shape().as_list())
    output = tf.transpose(output[-1], [1, 0, 2])
    last = tf.gather(output, int(output.get_shape()[0]) - 1)
    return tf.nn.softmax(tf.matmul(last, weight) + bias)


prediction = RNN(x, weight, bias)

# Define loss and optimizer
loss_f = -tf.reduce_sum(y * tf.log(prediction))
optimizer = tf.train.AdamOptimizer(learning_rate = learning_rate).minimize(loss_f)

# Evaluate model
correct_pred = tf.equal(tf.argmax(prediction,1), tf.argmax(y,1))
accuracy = tf.reduce_mean(tf.cast(correct_pred, tf.float32))
meanIOU = tf.metrics.mean_iou(tf.argmax(y, axis=1), tf.argmax(prediction, axis=1), n_classes, name='iou')


#####################################################
# Initializing the variables
init = tf.global_variables_initializer()
#IoU uses local variables (i.e. variables for temporary data)
local_init = tf.local_variables_initializer()

with tf.Session() as session:
    session.run(init)
    session.run(local_init)

    for epoch in range(epochs_num):
        training_accuracy_vec = []
        training_loss_vec = []

        tr_features, tr_labels = unison_shuffle(tr_features, tr_labels)
        # print('Data shapes after shuffling: train/test  data/lbl', tr_features.shape, tr_labels.shape,
        #       ts_features.shape, ts_labels.shape)
        # print('Labels: ', tr_labels)

        for itr in tqdm(range(training_iters), desc='Epoch %d' % int(epoch)):
            offset = (itr * batch_size) % (tr_labels.shape[0] - batch_size)
            batch_x = tr_features[offset:(offset + batch_size), :, :]
            batch_y = tr_labels[offset:(offset + batch_size), :]
            _, train_loss_val, train_acc_val = session.run([optimizer, loss_f, accuracy], feed_dict={x: batch_x, y: batch_y})
            training_accuracy_vec.append(train_acc_val)
            training_loss_vec.append(train_loss_val)

        acc = np.mean(training_accuracy_vec)
        # Calculate batch loss
        loss = np.mean(training_loss_vec)
        print( "Iter " + str(itr) + ", Minibatch Loss= " + \
        "{:.6f}".format(loss) + ", Training Accuracy= " + \
        "{:.5f}".format(acc))

        test_results = session.run([accuracy, meanIOU], feed_dict={x: ts_features, y: ts_labels})
        #NOTE: iou returns a tupple (meanIoU, confusion_matrix)
        print('Test accuracy: %5.3f / mean IoU: %5.3f ' % (test_results[0], test_results[1][0]))