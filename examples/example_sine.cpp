#include <cmath>
#include <fstream>
#include <iostream>
using namespace std;

namespace little_endian_io
{
  template <typename Word>
  std::ostream& write_word( std::ostream& outs, Word value, unsigned size = sizeof( Word ) )
  {
    for (; size; --size, value >>= 8)
      outs.put( static_cast <char> (value & 0xFF) );
    return outs;
  }
}
using namespace little_endian_io;

int main()
{
  ofstream f( "example.wav", ios::binary );
  int channels = 2;
  int sample_rate = 44100;
  int bps = 16;
  int total_rate = sample_rate * bps * channels / 8;
  int block_size = channels * bps / 8;

  // Write the file headers
  f << "RIFF----WAVEfmt ";     // (chunk size to be filled in later)
  write_word( f,         16, 4 );  // no extension data
  write_word( f,          1, 2 );  // PCM - integer samples
  write_word( f,      channels, 2 );  // two channels (stereo file)
  write_word( f,  sample_rate, 4 );  // samples per second (Hz)
  write_word( f,   total_rate, 4 );  // (Sample Rate * BitsPerSample * Channels) / 8
  write_word( f,   block_size, 2 );  // data block size (size of two integer samples, one for each channel, in bytes)
  write_word( f,          bps, 2 );  // number of bits per sample (use a multiple of 8)

  // Write the data chunk header
  size_t data_chunk_pos = f.tellp();
  f << "data----";  // (chunk size to be filled in later)
  
  // Write the audio samples
  // (We'll generate a single C4 note with a sine wave, fading from left to right)
  constexpr double two_pi = 6.283185307179586476925286766559;
  constexpr double max_amplitude = 32760;  // "volume"

  double hz        = sample_rate;    // samples per second
  double frequency = 261.626;  // middle C
  double seconds   = 2.5;      // time

  int N = hz * seconds;  // total number of samples
  for (int n = 0; n < N; n++)
  {
    double amplitude = (double)n / N * max_amplitude;
    double value     = sin( (two_pi * n * frequency) / hz );
    for(int ch=0; ch<channels; ch++) {
        if(ch==0) write_word( f, (int)(                 amplitude  * value), 2 );
        if(ch==1) write_word( f, (int)((max_amplitude - amplitude) * value), 2 );
    }
  }
  
  // (We'll need the final file size to fix the chunk sizes above)
  size_t file_length = f.tellp();

  // Fix the data chunk header to contain the data size
  f.seekp( data_chunk_pos + 4 );
  write_word( f, file_length - data_chunk_pos + 8 );

  // Fix the file header to contain the proper RIFF chunk size, which is (file size - 8) bytes
  f.seekp( 0 + 4 );
  write_word( f, file_length - 8, 4 ); 
}
