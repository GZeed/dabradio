#
/*
 *    Copyright (C) 2014 .. 2017
 *    Jan van Katwijk (J.vanKatwijk@gmail.com)
 *    Lazy Chair Computing
 *
 *    This file is part of the Qt-DAB program
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
 *    along with Qt-DAB if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include	"dab-processor.h"
#include	"fic-handler.h"
#include	"msc-handler.h"
#include	"radio.h"
#include	"dab-params.h"
//
/**
  *	\brief dabProcessor
  *	The dabProcessor class is the driver of the processing
  *	of the samplestream.
  *	It is the main interface to the qt-dab program,
  *	local are classes ofdmDecoder, ficHandler and mschandler.
  */

#define	C_LEVEL_SIZE	50
	dabProcessor::dabProcessor	(RadioInterface	*mr,
	                                 virtualInput	*theRig,
	                                 uint8_t	dabMode,
	                                 int16_t	threshold,
	                                 int16_t	diff_length,
	                                 QString	picturesPath,
	                                 RingBuffer<std::complex<float>>	*spectrumBuffer
	                                 ):
	                                 params (dabMode),
	                                 myReader (mr,
	                                           theRig,
	                                           spectrumBuffer
	                                 ),
	                                 my_ficHandler (mr, dabMode),
	                                 my_mscHandler (mr, dabMode,
	                                                picturesPath),
	                                 phaseSynchronizer (mr,
	                                                    dabMode, 
                                                            threshold,
	                                                    diff_length),
	                                 my_ofdmDecoder (mr,
	                                                 dabMode,
	                                                 theRig -> bitDepth (),
	                                                 &my_ficHandler,
	                                                 &my_mscHandler) {
int32_t	i;

	this	-> myRadioInterface	= mr;
	this	-> theRig		= theRig;
	this	-> T_null		= params. get_T_null ();
	this	-> T_s			= params. get_T_s ();
	this	-> T_u			= params. get_T_u ();
	this	-> T_F			= params. get_T_F ();
	this	-> nrBlocks		= params. get_L ();
	this	-> carriers		= params. get_carriers ();
	this	-> carrierDiff		= params. get_carrierDiff ();


	ofdmBuffer. resize (2 * T_s);
	ofdmBufferIndex			= 0;
	ofdmSymbolCount			= 0;
	tokenCount			= 0;
	fineCorrector			= 0;	
	f2Correction			= true;
	attempts			= 0;
	scanMode			= false;
	connect (this, SIGNAL (setSynced (char)),
	         myRadioInterface, SLOT (setSynced (char)));
	connect (this, SIGNAL (No_Signal_Found (void)),
	         myRadioInterface, SLOT (No_Signal_Found(void)));
	connect (this, SIGNAL (setSyncLost (void)),
	         myRadioInterface, SLOT (setSyncLost (void)));
	
	myReader. setRunning  (false);
//	the thread will be started from somewhere else
}

	dabProcessor::~dabProcessor	(void) {
	if (isRunning ()) {
	   myReader. setRunning (false);
	                                // exception to be raised
	                        	// through the getSample(s) functions.
	   msleep (100);
	   while (isRunning ()) {
	      usleep (100);
	   }
	}
}

/***
   *	\brief run
   *	The main thread, reading samples,
   *	time synchronization and frequency synchronization
   *	Identifying blocks in the DAB frame
   *	and sending them to the ofdmDecoder who will transfer the results
   *	Finally, estimating the small freqency error
   */
void	dabProcessor::run	(void) {
int32_t		startIndex;
int32_t		i;
std::complex<float>	FreqCorr;
int32_t		counter;
float		cLevel;
int32_t		syncBufferIndex	= 0;
const
int32_t		syncBufferSize	= 32768;
const
int32_t		syncBufferMask	= syncBufferSize - 1;
float		envBuffer	[syncBufferSize];

        fineCorrector   = 0;
        f2Correction    = true;
        syncBufferIndex = 0;
	attempts	= 0;
        theRig  -> resetBuffer ();
	coarseOffset	= theRig -> getOffset ();
	myReader. setRunning (true);
	my_ofdmDecoder. start ();
//
//	to get some idea of the signal strength
	try {
	   for (i = 0; i < T_F / 5; i ++) {
	      myReader. getSample (0);
	   }
Initing:
notSynced:
	   syncBufferIndex	= 0;
	   cLevel		= 0;

	   for (i = 0; i < C_LEVEL_SIZE; i ++) {
	      std::complex<float> sample	= myReader. getSample (0);
	      envBuffer [syncBufferIndex]	= jan_abs (sample);
	      cLevel	 			+= envBuffer [syncBufferIndex];
	      syncBufferIndex ++;
	   }
/**
  *	We now have initial values for cLevel (i.e. the sum
  *	over the last C_LEVEL_SIZE samples) and sLevel, the long term average.
  */
SyncOnNull:
/**
  *	here we start looking for the null level, i.e. a dip
  */
	   counter	= 0;
	   setSynced (false);
	   while (cLevel / C_LEVEL_SIZE  > 0.40 * myReader. get_sLevel ()) {
	      std::complex<float> sample	=
	                      myReader. getSample (coarseOffset + fineCorrector);
	      envBuffer [syncBufferIndex] = jan_abs (sample);
//	update the levels
	      cLevel += envBuffer [syncBufferIndex] -
	                envBuffer [(syncBufferIndex - C_LEVEL_SIZE) & syncBufferMask];
	      syncBufferIndex = (syncBufferIndex + 1) & syncBufferMask;
	      counter ++;
	      if (counter > T_F) { // hopeless
	         if (scanMode && (++ attempts >= 5)) {
	            emit (No_Signal_Found ());
                    attempts = 0;
                 }

	         goto notSynced;
	      }
	   }
/**
  *	It seemed we found a dip that started app 65/100 * 50 samples earlier.
  *	We now start looking for the end of the null period.
  */
	   counter	= 0;
SyncOnEndNull:
	   while (cLevel / C_LEVEL_SIZE < 0.75 * myReader. get_sLevel ()) {
	      std::complex<float> sample =
	              myReader. getSample (coarseOffset + fineCorrector);
	      envBuffer [syncBufferIndex] = jan_abs (sample);
//	update the levels
	      cLevel += envBuffer [syncBufferIndex] -
	                envBuffer [(syncBufferIndex - C_LEVEL_SIZE) & syncBufferMask];
	      syncBufferIndex = (syncBufferIndex + 1) & syncBufferMask;
	      counter	++;
//
	      if (counter > T_null + 50) { // hopeless
	         goto notSynced;
	      }
	   }
/**
  *	The end of the null period is identified, the actual end
  *	is probably about 40 samples earlier.
  */
SyncOnPhase:
/**
  *	We now have to find the exact first sample of the non-null period.
  *	We use a correlation that will find the first sample after the
  *	cyclic prefix.
  *	When in "sync", i.e. pretty sure that we know were we are,
  *	we skip the "dip" identification and come here right away.
  *
  *	now read in Tu samples. The precise number is not really important
  *	as long as we can be sure that the first sample to be identified
  *	is part of the samples read.
  */
	myReader. getSamples (ofdmBuffer. data (),
	                        T_u, coarseOffset + fineCorrector);
//
//	and then, call upon the phase synchronizer to verify/compute
//	the real "first" sample
	   startIndex = phaseSynchronizer. findIndex (ofdmBuffer);
	   if (startIndex < 0) { // no sync, try again
	      if (!f2Correction) {
	         setSyncLost ();
	      }
	      goto notSynced;
	   }
/**
  *	Once here, we are synchronized, we need to copy the data we
  *	used for synchronization for block 0
  */
	   memmove (ofdmBuffer. data (),
	            &((ofdmBuffer. data ()) [startIndex]),
	                  (T_u - startIndex) * sizeof (std::complex<float>));
	   ofdmBufferIndex	= T_u - startIndex;

Block_0:
/**
  *	Block 0 is special in that it is used for fine time synchronization,
  *	for coarse frequency synchronization
  *	and its content is used as a reference for decoding the
  *	first datablock.
  *	We read the missing samples in the ofdm buffer
  */
	   setSynced (true);
	   myReader. getSamples (&((ofdmBuffer. data ()) [ofdmBufferIndex]),
	                           T_u - ofdmBufferIndex,
	                           coarseOffset + fineCorrector);
	   my_ofdmDecoder. processBlock_0 (ofdmBuffer);

//	Here we look only at the block_0 when we need a coarse
//	frequency synchronization.
	   f2Correction	= !my_ficHandler. syncReached ();
	   if (f2Correction) {
	      int correction	=
	            phaseSynchronizer. estimate_CarrierOffset (ofdmBuffer);
	      if (correction != 100) {
	         coarseOffset	+= correction * carrierDiff;
	         if (abs (coarseOffset) > Khz (35))
	            coarseOffset = 0;
	      }
	   }
/**
  *	after block 0, we will just read in the other (params -> L - 1) blocks
  */
Data_blocks:
/**
  *	The first ones are the FIC blocks. We immediately
  *	start with building up an average of the phase difference
  *	between the samples in the cyclic prefix and the
  *	corresponding samples in the datapart.
  */
	   FreqCorr	= std::complex<float> (0, 0);
	   for (ofdmSymbolCount = 1;
	        ofdmSymbolCount < 4; ofdmSymbolCount ++) {
	      myReader. getSamples (ofdmBuffer. data (),
	                              T_s, coarseOffset + fineCorrector);
	      for (i = (int)T_u; i < (int)T_s; i ++) 
	         FreqCorr += ofdmBuffer [i] * conj (ofdmBuffer [i - T_u]);

	      my_ofdmDecoder. decodeFICblock (ofdmBuffer, ofdmSymbolCount);
	   }

///	and similar for the (params -> L - 4) MSC blocks
	   for (ofdmSymbolCount = 4;
	        ofdmSymbolCount <  (uint16_t)nrBlocks;
	        ofdmSymbolCount ++) {
	      myReader. getSamples (ofdmBuffer. data (),
	                              T_s, coarseOffset + fineCorrector);
	      for (i = (int32_t)T_u; i < (int32_t)T_s; i ++) 
	         FreqCorr += ofdmBuffer [i] * conj (ofdmBuffer [i - T_u]);

	      my_ofdmDecoder. decodeMscblock (ofdmBuffer, ofdmSymbolCount);
	   }

NewOffset:
///	we integrate the newly found frequency error with the
///	existing frequency error.
	   fineCorrector += 0.1 * arg (FreqCorr) / (2 * M_PI) * carrierDiff;
//
/**
  *	OK,  here we are at the end of the frame
  *	Assume everything went well and skip T_null samples
  */
	   syncBufferIndex	= 0;
	   cLevel		= 0;
	   myReader. getSamples (ofdmBuffer. data (),
	                         T_null, coarseOffset);
/**
  *	The first sample to be found for the next frame should be T_g
  *	samples ahead. Before going for the next frame, we
  *	we just check the fineCorrector
  */
	   if (fineCorrector > carrierDiff / 2) {
	      coarseOffset += carrierDiff;
	      fineCorrector -= carrierDiff;
	   }
	   else
	   if (fineCorrector < -carrierDiff / 2) {
	      coarseOffset -= carrierDiff;
	      fineCorrector += carrierDiff;
	   }
ReadyForNewFrame:
///	and off we go, up to the next frame
	   counter	= 0;
	   goto SyncOnPhase;
	}
	catch (int e) {
	   fprintf (stderr, "dabProcessor is stopping\n");
	   ;
	}
	my_ofdmDecoder. stop ();
	my_mscHandler.  stop ();
	my_ficHandler.  stop ();
}

void	dabProcessor:: reset	(void) {
	myReader. setRunning (false);
	while (isRunning ())
	   wait ();
	usleep (10000);
	my_ofdmDecoder. stop ();
	my_mscHandler.  reset ();
	my_ficHandler.  reset ();
	start ();
}

void	dabProcessor::stop	(void) {
	myReader. setRunning (false);
	while (isRunning ())
	   wait ();
	usleep (10000);
	my_ofdmDecoder. stop ();
	my_mscHandler.  reset ();
	my_ficHandler.  reset ();
}

void	dabProcessor::coarseCorrectorOn (void) {
	f2Correction 	= true;
	coarseOffset	= 0;
}

void	dabProcessor::coarseCorrectorOff (void) {
	f2Correction	= false;
	theRig	-> setOffset (coarseOffset);
}

void	dabProcessor::set_scanMode	(bool b) {
	scanMode	= b;
	attempts	= 0;
}

//	we could have derive the dab processor from fic and msc handlers,
//	however, from a logical point of view they are more delegates than
//	parents.
uint8_t dabProcessor::kindofService           (QString &s) {
	return my_ficHandler. kindofService (s);
}

void	dabProcessor::dataforAudioService     (int16_t c, audiodata *dd) {
	my_ficHandler. dataforAudioService (c, dd);
}

void	dabProcessor::dataforAudioService     (QString &s,audiodata *dd) {
	my_ficHandler. dataforAudioService (s, dd, 0);
}

void	dabProcessor::dataforAudioService     (QString &s,
	                                          audiodata *d, int16_t c) {
	my_ficHandler. dataforAudioService (s, d, c);
}

void	dabProcessor::dataforDataService	(int16_t c, packetdata *dd) {
	my_ficHandler. dataforDataService (c, dd);
}

void	dabProcessor::dataforDataService	(QString &s, packetdata *dd) {
	my_ficHandler. dataforDataService (s, dd, 0);
}

void	dabProcessor::dataforDataService	(QString &s,
	                                            packetdata *d, int16_t c) {
	my_ficHandler. dataforDataService (s, d, c);
}


void	dabProcessor::reset_msc (void) {
	my_mscHandler. reset ();
}

void	dabProcessor::set_audioChannel (audiodata *d,
	                                      RingBuffer<int16_t> *b) {
	my_mscHandler. set_audioChannel (d, b);
}

void	dabProcessor::set_dataChannel (packetdata *d,
	                                      RingBuffer<uint8_t> *b) {
	my_mscHandler. set_dataChannel (d, b);
}

int32_t dabProcessor::get_ensembleId	(void) {
	return my_ficHandler. get_ensembleId ();
}

QString dabProcessor::get_ensembleName	(void) {
	return my_ficHandler. get_ensembleName ();
}

void	dabProcessor::clearEnsemble	(void) {
	my_ficHandler. clearEnsemble ();
}

	
