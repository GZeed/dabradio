#
/*
 *    Copyright (C) 2013 .. 2017
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Computing
 *
 *    This file is part of Qt-DAB
 *    Qt-DAB is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    Qt-DAB is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with Qt-DAB; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *	Once the bits are "in", interpretation and manipulation
 *	should reconstruct the data blocks.
 *	Ofdm_decoder is called once every Ts samples, and
 *	its invocation results in 2 * Tu bits
 */
#include	<vector>
#include	"ofdm-decoder.h"
#include	"radio.h"
#include	"phasetable.h"
#include	"fic-handler.h"
#include	"msc-handler.h"
#include	"freq-interleaver.h"
#include	"dab-params.h"

/**
  *	\brief ofdmDecoder
  *	The class ofdmDecoder is
  *	taking the data from the ofdmProcessor class in, and
  *	will extract the Tu samples, do an FFT and extract the
  *	carriers and map them on (soft) bits
  *	In the threaded version, a thread will run, fetch the
  *	data blocks asynchronously and do all computations.
  *	Threading is needed for e.g. the RPI 2 version
  *	In the non-threaded version, the functions in the class
  *	are just executed in the caller's thread
  */
	ofdmDecoder::ofdmDecoder	(RadioInterface *mr,
	                                 uint8_t	dabMode,
	                                 int16_t	bitDepth,
	                                 ficHandler	*my_ficHandler,
	                                 mscHandler	*my_mscHandler):
	                                    params (dabMode),
	                                    my_fftHandler (dabMode),
#ifdef	__THREADED_DECODING
	                                    bufferSpace (params. get_L ()),
#endif
	                                    myMapper (dabMode) {
int16_t	i;
	this	-> myRadioInterface	= mr;
//
	this	-> my_ficHandler	= my_ficHandler;
	this	-> my_mscHandler	= my_mscHandler;
	this	-> T_s			= params. get_T_s ();
	this	-> T_u			= params. get_T_u ();
	this	-> nrBlocks		= params. get_L ();
	this	-> carriers		= params. get_carriers ();
	ibits. resize (2 * this -> carriers);

	this	-> T_g			= T_s - T_u;
	fft_buffer			= my_fftHandler. getVector ();
	phaseReference			.resize (T_u);

	connect (this, SIGNAL (show_snr (int)),
	         mr, SLOT (show_snr (int)));
	snrCount		= 0;
	snr			= 0;	

#ifdef __THREADED_DECODING
/**
  *	When implemented in a thread, the thread controls the
  *	reading in of the data and processing the data through
  *	functions for handling block 0, FIC blocks and MSC blocks.
  *
  *	We just create a large buffer where index i refers to block i.
  *
  */
	command			= new std::complex<float> * [nrBlocks];
	for (i = 0; i < nrBlocks; i ++)
	   command [i] = new std::complex<float> [T_u];
	amount		= 0;
#endif
}

	ofdmDecoder::~ofdmDecoder	(void) {
int16_t	i;
#ifdef	__THREADED_DECODING
	running. store (false);
	while (isRunning ()) {
	   commandHandler. wakeAll ();
	   usleep (1000);
	}
	for (i = 0; i < nrBlocks; i ++)
	   delete[] command [i];
	delete[]	command;
#endif

}
//
//	the client of this class should not know whether
//	we run with a separate thread or not,
#ifndef	__THREADED_DECODING
void	ofdmDecoder::start	(void) {
}
#endif

void	ofdmDecoder::stop	(void) {
#ifdef	__THREADED_DECODING
	running. store (false);
	while (isRunning ()) {
	   commandHandler. wakeAll ();
	   usleep (1000);
	}
#endif
}

void	ofdmDecoder::reset	(void) {
#ifdef	__THREADED_DECODING
	stop  ();
	usleep (10000);
	start ();
#endif
}

//

#ifdef	__THREADED_DECODING
/**
  *	The code in the thread executes a simple loop,
  *	waiting for the next block and executing the interpretation
  *	operation for that block.
  *	In our original code the block count was 1 higher than
  *	our count here.
  */
void	ofdmDecoder::run	(void) {
int16_t	currentBlock	= 0;

	running. store (true);
	while (running. load ()) {
	   helper. lock ();
	   commandHandler. wait (&helper, 100);
	   helper. unlock ();
	   while ((amount > 0) && running. load ()) {
	      if (currentBlock == 0)
	         processBlock_0 ();
	      else
	      if (currentBlock < 4)
	         decodeFICblock (currentBlock);
	      else
	         decodeMscblock (currentBlock);
	      bufferSpace. release (1);
	      helper. lock ();
	      currentBlock = (currentBlock + 1) % (nrBlocks);
	      amount -= 1;
	      helper. unlock ();
	   }
	}
	fprintf (stderr, "ofdm decoder is closing down now\n");
}
/**
  *	We need some functions to enter the ofdmProcessor data
  *	in the buffer.
  */
void	ofdmDecoder::processBlock_0 (std::vector<std::complex<float>>vi) {
	bufferSpace. acquire (1);
	memcpy (command [0], vi. data (), sizeof (std::complex<float>) * T_u);
	helper. lock ();
	amount ++;
	commandHandler. wakeOne ();
	helper. unlock ();
}

void	ofdmDecoder::decodeFICblock (std::vector<std::complex<float>> vi,
		                                             int32_t blkno) {
	bufferSpace. acquire (1);
	memcpy (command [blkno], &((vi. data ()) [T_g]),
	                                  sizeof (std::complex<float>) * T_u);
	helper. lock ();
	amount ++;
	commandHandler. wakeOne ();
	helper. unlock ();
}

void	ofdmDecoder::decodeMscblock (std::vector<std::complex<float> > vi,
	                                                     int32_t blkno) {
	bufferSpace. acquire (1);
	memcpy (command [blkno], &((vi. data ()) [T_g]),
	                                   sizeof (std::complex<float>) * T_u);
	helper. lock ();
	amount ++;
	commandHandler. wakeOne ();
	helper. unlock ();
}
#endif
/**
  *	Note that the distinction, made in the ofdmProcessor class
  *	does not add much here, iff we decide to choose the multi core
  *	option definitely, then code may be simplified there.
  */

/**
  *	handle block 0 as collected from the buffer
  */
#ifdef	__THREADED_DECODING
void	ofdmDecoder::processBlock_0 (void) {

	memcpy (fft_buffer, command [0], T_u * sizeof (std::complex<float>));
#else
void	ofdmDecoder::processBlock_0 (std::vector <std::complex<float> > buffer) {
	memcpy (fft_buffer, buffer. data (),
	                             T_u * sizeof (std::complex<float>));
#endif

	my_fftHandler. do_FFT ();
/**
  *	The SNR is determined by looking at a segment of bins
  *	within the signal region and bits outside.
  *	It is just an indication
  */

	if (++snrCount > 10) {
	   snr	= 0.8 * snr + 0.2 * get_snr (fft_buffer);
	   show_snr (snr);
	   snrCount = 0;
	}
/**
  *	we are now in the frequency domain, and we keep the carriers
  *	as coming from the FFT as phase reference.
  */
	memcpy (phaseReference. data (), fft_buffer,
	                   T_u * sizeof (std::complex<float>));
}
//
/**
  *	for the other blocks of data, the first step is to go from
  *	time to frequency domain, to get the carriers.
  *	we distinguish between FIC blocks and other blocks,
  *	only to spare a test. The mapping code is the same
  */
static
int	cnt	= 0;
#ifdef	__THREADED_DECODING
void	ofdmDecoder::decodeFICblock (int32_t blkno) {
int16_t	i;
	memcpy (fft_buffer, command [blkno], T_u * sizeof (std::complex<float>));
#else
void	ofdmDecoder::decodeFICblock (std::vector <std::complex<float>> buffer,
	                                                int32_t blkno) {
int16_t	i;
	memcpy (fft_buffer, &((buffer. data ()) [T_g]),
	                               T_u * sizeof (std::complex<float>));
#endif


fftlabel:
/**
  *	first step: do the FFT
  */
	my_fftHandler. do_FFT ();
/**
  *	a little optimization: we do not interchange the
  *	positive/negative frequencies to their right positions.
  *	The de-interleaving understands this
  */
toBitsLabel:
/**
  *	Note that from here on, we are only interested in the
  *	"carriers" useful carriers of the FFT output
  */

	for (i = 0; i < carriers; i ++) {
	   int16_t	index	= myMapper.  mapIn (i);
	   if (index < 0) 
	      index += T_u;
/**
  *	decoding is computing the phase difference between
  *	carriers with the same index in subsequent blocks.
  *	The carrier of a block is the reference for the carrier
  *	on the same position in the next block
  */
	   std::complex<float>	r1 = fft_buffer [index] *
	                                    conj (phaseReference [index]);
	   float ab1	= jan_abs (r1);
//	split the real and the imaginary part and scale it
//	we make the bits into softbits in the range -127 .. 127
	   ibits [i]		=  - real (r1) / ab1 * 127.0;
	   ibits [carriers + i] =  - imag (r1) / ab1 * 127.0;
	}
	memcpy (phaseReference. data (), fft_buffer,
	                            T_u * sizeof (std::complex<float>));

handlerLabel:
	my_ficHandler -> process_ficBlock (ibits, blkno);
}
/**
  *	Msc block decoding is equal to FIC block decoding,
  *	further processing is different though
  */
#ifdef	__THREADED_DECODING
void	ofdmDecoder::decodeMscblock (int32_t blkno) {
int16_t	i;

	memcpy (fft_buffer, command [blkno], T_u * sizeof (std::complex<float>));
#else
void	ofdmDecoder::decodeMscblock (std::vector <std::complex<float>>buffer,
	                                                  int32_t blkno) {
int16_t	i;
	memcpy (fft_buffer, &((buffer. data ())[T_g]), T_u * sizeof (std::complex<float>));
#endif

fftLabel:
	my_fftHandler. do_FFT ();
//
//	Note that "mapIn" maps to -carriers / 2 .. carriers / 2
//	we did not set the fft output to low .. high
toBitsLabel:
	for (i = 0; i < carriers; i ++) {
	   int16_t	index	= myMapper. mapIn (i);
	   if (index < 0) 
	      index += T_u;
	      
	   std::complex<float>	r1 = fft_buffer [index] *
	                               conj (phaseReference [index]);
	   float ab1	= jan_abs (r1);
//	Recall:  the viterbi decoder wants 127 max pos, - 127 max neg
//	we make the bits into softbits in the range -127 .. 127
	   ibits [i]		=  - real (r1) / ab1 * 127.0;
	   ibits [carriers + i] =  - imag (r1) / ab1 * 127.0;
	}

	memcpy (phaseReference. data (), fft_buffer,
	                           T_u * sizeof (std::complex<float>));

handlerLabel:;
	my_mscHandler -> process_mscBlock (ibits, blkno);
}

//
//
/**
  *	for the snr we have a full T_u wide vector, with in the middle
  *	K carriers.
  *	Just get the strength from the selected carriers compared
  *	to the strength of the carriers outside that region
  */
int16_t	ofdmDecoder::get_snr (std::complex<float>  *v) {
int16_t	i;
float	noise 	= 0;
float	signal	= 0;

	for (i = -100; i < 100; i ++)
	   noise += abs (v [(T_u / 2 + i)]);

	noise	/= 200;
	for (i =  - carriers / 4;  i <  carriers / 4; i ++)
	   signal += abs (v [(T_u + i) % T_u]);
	signal	/= (carriers / 2);

	return 20 * log10 ((signal + 0.005) / (noise + 0.005));
}


